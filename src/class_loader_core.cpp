/*
 * Copyright (c) 2012, Willow Garage, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Willow Garage, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "cnr_class_loader/class_loader_core.hpp"
#include "cnr_class_loader/class_loader.hpp"

#include <boost/filesystem.hpp>

#include <Poco/SharedLibrary.h>

#include <cassert>
#include <cstddef>
#include <string>
#include <vector>

namespace cnr_class_loader
{
namespace impl
{


// Global data

boost::recursive_mutex & getLoadedLibraryVectorMutex()
{
  static boost::recursive_mutex m;
  return m;
}

boost::recursive_mutex & getPluginBaseToFactoryMapMapMutex()
{
  static boost::recursive_mutex m;
  return m;
}

BaseToFactoryMapMap & getGlobalPluginBaseToFactoryMapMap()
{
  static BaseToFactoryMapMap instance;
  return instance;
}

FactoryMap & getFactoryMapForBaseClass(const std::string & typeid_base_class_name)
{
  BaseToFactoryMapMap & factoryMapMap = getGlobalPluginBaseToFactoryMapMap();
  std::string base_class_name = typeid_base_class_name;
  if (factoryMapMap.find(base_class_name) == factoryMapMap.end()) {
    factoryMapMap[base_class_name] = FactoryMap();
  }

  return factoryMapMap[base_class_name];
}

MetaObjectVector & getMetaObjectGraveyard()
{
  static MetaObjectVector instance;
  return instance;
}

LibraryVector & getLoadedLibraryVector()
{
  static LibraryVector instance;
  return instance;
}

std::string & getCurrentlyLoadingLibraryNameReference()
{
  static std::string library_name;
  return library_name;
}

std::string getCurrentlyLoadingLibraryName()
{
  return getCurrentlyLoadingLibraryNameReference();
}

void setCurrentlyLoadingLibraryName(const std::string & library_name)
{
  std::string & library_name_ref = getCurrentlyLoadingLibraryNameReference();
  library_name_ref = library_name;
}

ClassLoader * & getCurrentlyActiveClassLoaderReference()
{
  static ClassLoader * loader = nullptr;
  return loader;
}

ClassLoader * getCurrentlyActiveClassLoader()
{
  return getCurrentlyActiveClassLoaderReference();
}

void setCurrentlyActiveClassLoader(ClassLoader * loader)
{
  ClassLoader * & loader_ref = getCurrentlyActiveClassLoaderReference();
  loader_ref = loader;
}

bool & hasANonPurePluginLibraryBeenOpenedReference()
{
  static bool hasANonPurePluginLibraryBeenOpenedReference = false;
  return hasANonPurePluginLibraryBeenOpenedReference;
}

bool hasANonPurePluginLibraryBeenOpened()
{
  return hasANonPurePluginLibraryBeenOpenedReference();
}

void hasANonPurePluginLibraryBeenOpened(bool hasIt)
{
  hasANonPurePluginLibraryBeenOpenedReference() = hasIt;
}


// MetaObject search/insert/removal/query

MetaObjectVector allMetaObjects(const FactoryMap & factories)
{
  MetaObjectVector all_meta_objs;
  for (auto & it : factories) {
    all_meta_objs.push_back(it.second);
  }
  return all_meta_objs;
}

MetaObjectVector allMetaObjects()
{
  boost::recursive_mutex::scoped_lock lock(getPluginBaseToFactoryMapMapMutex());

  MetaObjectVector all_meta_objs;
  BaseToFactoryMapMap & factory_map_map = getGlobalPluginBaseToFactoryMapMap();
  BaseToFactoryMapMap::iterator itr;

  for (auto & it : factory_map_map) {
    MetaObjectVector objs = allMetaObjects(it.second);
    all_meta_objs.insert(all_meta_objs.end(), objs.begin(), objs.end());
  }
  return all_meta_objs;
}

MetaObjectVector
filterAllMetaObjectsOwnedBy(const MetaObjectVector & to_filter, const ClassLoader * owner)
{
  MetaObjectVector filtered_objs;
  for (auto & f : to_filter) {
    if (f->isOwnedBy(owner)) {
      filtered_objs.push_back(f);
    }
  }
  return filtered_objs;
}

MetaObjectVector
filterAllMetaObjectsAssociatedWithLibrary(
  const MetaObjectVector & to_filter, const std::string & library_path)
{
  MetaObjectVector filtered_objs;
  for (auto & f : to_filter) {
    if (f->getAssociatedLibraryPath() == library_path) {
      filtered_objs.push_back(f);
    }
  }
  return filtered_objs;
}

MetaObjectVector
allMetaObjectsForClassLoader(const ClassLoader * owner)
{
  return filterAllMetaObjectsOwnedBy(allMetaObjects(), owner);
}

MetaObjectVector
allMetaObjectsForLibrary(const std::string & library_path)
{
  return filterAllMetaObjectsAssociatedWithLibrary(allMetaObjects(), library_path);
}

MetaObjectVector
allMetaObjectsForLibraryOwnedBy(const std::string & library_path, const ClassLoader * owner)
{
  return filterAllMetaObjectsOwnedBy(allMetaObjectsForLibrary(library_path), owner);
}

void insertMetaObjectIntoGraveyard(AbstractMetaObjectBase * meta_obj)
{
  CONSOLE_BRIDGE_logDebug(
    "class_loader.impl: "
    "Inserting MetaObject (class = %s, base_class = %s, ptr = %p) into graveyard",
    meta_obj->className().c_str(), meta_obj->baseClassName().c_str(),
    reinterpret_cast<void *>(meta_obj));
  getMetaObjectGraveyard().push_back(meta_obj);
}

void destroyMetaObjectsForLibrary(
  const std::string & library_path, FactoryMap & factories, const ClassLoader * loader)
{
  FactoryMap::iterator factory_itr = factories.begin();
  while (factory_itr != factories.end()) {
    AbstractMetaObjectBase * meta_obj = factory_itr->second;
    if (meta_obj->getAssociatedLibraryPath() == library_path && meta_obj->isOwnedBy(loader)) {
      meta_obj->removeOwningClassLoader(loader);
      if (!meta_obj->isOwnedByAnybody()) {
        FactoryMap::iterator factory_itr_copy = factory_itr;
        factory_itr++;
        // TODO(mikaelarguedas) fix this when branching out for melodic
        // Note: map::erase does not return iterator like vector::erase does.
        // Hence the ugliness of this code and a need for copy. Should be fixed in next C++ revision
        factories.erase(factory_itr_copy);

        // Insert into graveyard
        // We remove the metaobject from its factory map, but we don't destroy it...instead it
        // saved to a "graveyard" to the side.
        // This is due to our static global variable initialization problem that causes factories
        // to not be registered when a library is closed and then reopened.
        // This is because it's truly not closed due to the use of global symbol binding i.e.
        // calling dlopen with RTLD_GLOBAL instead of RTLD_LOCAL.
        // We require using the former as the which is required to support RTTI
        insertMetaObjectIntoGraveyard(meta_obj);
      } else {
        ++factory_itr;
      }
    } else {
      ++factory_itr;
    }
  }
}

void destroyMetaObjectsForLibrary(const std::string & library_path, const ClassLoader * loader)
{
  boost::recursive_mutex::scoped_lock lock(getPluginBaseToFactoryMapMapMutex());

  CONSOLE_BRIDGE_logDebug(
    "class_loader.impl: "
    "Removing MetaObjects associated with library %s and class loader %p from global "
    "plugin-to-factorymap map.\n",
    library_path.c_str(), reinterpret_cast<const void *>(loader));

  // We have to walk through all FactoryMaps to be sure
  BaseToFactoryMapMap & factory_map_map = getGlobalPluginBaseToFactoryMapMap();
  for (auto & it : factory_map_map) {
    destroyMetaObjectsForLibrary(library_path, it.second, loader);
  }

  CONSOLE_BRIDGE_logDebug("%s", "class_loader.impl: Metaobjects removed.");
}

bool areThereAnyExistingMetaObjectsForLibrary(const std::string & library_path)
{
  return allMetaObjectsForLibrary(library_path).size() > 0;
}

// Loaded Library Vector manipulation
LibraryVector::iterator findLoadedLibrary(const std::string & library_path)
{
  LibraryVector & open_libraries = getLoadedLibraryVector();
  for (auto it = open_libraries.begin(); it != open_libraries.end(); ++it) {
    if (it->first == library_path) {
      return it;
    }
  }
  return open_libraries.end();
}

bool isLibraryLoadedByAnybody(const std::string & library_path)
{
  boost::recursive_mutex::scoped_lock lock(getLoadedLibraryVectorMutex());

  LibraryVector & open_libraries = getLoadedLibraryVector();
  LibraryVector::iterator itr = findLoadedLibrary(library_path);

  if (itr != open_libraries.end()) {
    assert(itr->second->isLoaded() == true);  // Ensure Poco actually thinks the library is loaded
    return true;
  } else {
    return false;
  }
}

bool isLibraryLoaded(const std::string & library_path, ClassLoader * loader)
{
  bool is_lib_loaded_by_anyone = isLibraryLoadedByAnybody(library_path);
  size_t num_meta_objs_for_lib = allMetaObjectsForLibrary(library_path).size();
  size_t num_meta_objs_for_lib_bound_to_loader =
    allMetaObjectsForLibraryOwnedBy(library_path, loader).size();
  bool are_meta_objs_bound_to_loader =
    (0 == num_meta_objs_for_lib) ? true : (
    num_meta_objs_for_lib_bound_to_loader <= num_meta_objs_for_lib);

  return is_lib_loaded_by_anyone && are_meta_objs_bound_to_loader;
}

std::vector<std::string> getAllLibrariesUsedByClassLoader(const ClassLoader * loader)
{
  MetaObjectVector all_loader_meta_objs = allMetaObjectsForClassLoader(loader);
  std::vector<std::string> all_libs;
  for (auto & meta_obj : all_loader_meta_objs) {
    std::string lib_path = meta_obj->getAssociatedLibraryPath();
    if (std::find(all_libs.begin(), all_libs.end(), lib_path) == all_libs.end()) {
      all_libs.push_back(lib_path);
    }
  }
  return all_libs;
}


// Implementation of Remaining Core plugin impl Functions

void addClassLoaderOwnerForAllExistingMetaObjectsForLibrary(
  const std::string & library_path, ClassLoader * loader)
{
  MetaObjectVector all_meta_objs = allMetaObjectsForLibrary(library_path);
  for (auto & meta_obj : all_meta_objs) {
    CONSOLE_BRIDGE_logDebug(
      "class_loader.impl: "
      "Tagging existing MetaObject %p (base = %s, derived = %s) with "
      "class loader %p (library path = %s).",
      reinterpret_cast<void *>(meta_obj), meta_obj->baseClassName().c_str(),
      meta_obj->className().c_str(),
      reinterpret_cast<void *>(loader),
      nullptr == loader ? loader->getLibraryPath().c_str() : "NULL");
    meta_obj->addOwningClassLoader(loader);
  }
}

void revivePreviouslyCreateMetaobjectsFromGraveyard(
  const std::string & library_path, ClassLoader * loader)
{
  boost::recursive_mutex::scoped_lock b2fmm_lock(getPluginBaseToFactoryMapMapMutex());
  MetaObjectVector & graveyard = getMetaObjectGraveyard();

  for (auto & obj : graveyard) {
    if (obj->getAssociatedLibraryPath() == library_path) {
      CONSOLE_BRIDGE_logDebug(
        "class_loader.impl: "
        "Resurrected factory metaobject from graveyard, class = %s, base_class = %s ptr = %p..."
        "bound to ClassLoader %p (library path = %s)",
        obj->className().c_str(), obj->baseClassName().c_str(), reinterpret_cast<void *>(obj),
        reinterpret_cast<void *>(loader),
        nullptr == loader ? loader->getLibraryPath().c_str() : "NULL");

      obj->addOwningClassLoader(loader);
      assert(obj->typeidBaseClassName() != "UNSET");
      FactoryMap & factory = getFactoryMapForBaseClass(obj->typeidBaseClassName());
      factory[obj->className()] = obj;
    }
  }
}

void purgeGraveyardOfMetaobjects(
  const std::string & library_path, ClassLoader * loader, bool delete_objs)
{
  MetaObjectVector all_meta_objs = allMetaObjects();
  // Note: Lock must happen after call to allMetaObjects as that will lock
  boost::recursive_mutex::scoped_lock b2fmm_lock(getPluginBaseToFactoryMapMapMutex());

  MetaObjectVector & graveyard = getMetaObjectGraveyard();
  MetaObjectVector::iterator itr = graveyard.begin();

  while (itr != graveyard.end()) {
    AbstractMetaObjectBase * obj = *itr;
    if (obj->getAssociatedLibraryPath() == library_path) {
      CONSOLE_BRIDGE_logDebug(
        "class_loader.impl: "
        "Purging factory metaobject from graveyard, class = %s, base_class = %s ptr = %p.."
        ".bound to ClassLoader %p (library path = %s)",
        obj->className().c_str(), obj->baseClassName().c_str(), reinterpret_cast<void *>(obj),
        reinterpret_cast<void *>(loader),
        nullptr == loader ? loader->getLibraryPath().c_str() : "NULL");

      bool is_address_in_graveyard_same_as_global_factory_map =
        std::find(all_meta_objs.begin(), all_meta_objs.end(), *itr) != all_meta_objs.end();
      itr = graveyard.erase(itr);
      if (delete_objs) {
        if (is_address_in_graveyard_same_as_global_factory_map) {
          CONSOLE_BRIDGE_logDebug("%s",
            "class_loader.impl: "
            "Newly created metaobject factory in global factory map map has same address as "
            "one in graveyard -- metaobject has been purged from graveyard but not deleted.");
        } else {
          assert(hasANonPurePluginLibraryBeenOpened() == false);
          CONSOLE_BRIDGE_logDebug(
            "class_loader.impl: "
            "Also destroying metaobject %p (class = %s, base_class = %s, library_path = %s) "
            "in addition to purging it from graveyard.",
            reinterpret_cast<void *>(obj), obj->className().c_str(), obj->baseClassName().c_str(),
            obj->getAssociatedLibraryPath().c_str());
#ifndef _WIN32
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
#endif
          delete (obj);  // Note: This is the only place where metaobjects can be destroyed
#ifndef _WIN32
#pragma GCC diagnostic pop
#endif
        }
      }
    } else {
      itr++;
    }
  }
}

void loadLibrary(const std::string & library_path, ClassLoader * loader)
{
  static boost::recursive_mutex loader_mutex;
  CONSOLE_BRIDGE_logDebug(
    "class_loader.impl: "
    "Attempting to load library %s on behalf of ClassLoader handle %p...\n",
    library_path.c_str(), reinterpret_cast<void *>(loader));
  boost::recursive_mutex::scoped_lock loader_lock(loader_mutex);

  // If it's already open, just update existing metaobjects to have an additional owner.
  if (isLibraryLoadedByAnybody(library_path)) {
    boost::recursive_mutex::scoped_lock lock(getPluginBaseToFactoryMapMapMutex());
    CONSOLE_BRIDGE_logDebug("%s",
      "class_loader.impl: "
      "Library already in memory, but binding existing MetaObjects to loader if necesesary.\n");
    addClassLoaderOwnerForAllExistingMetaObjectsForLibrary(library_path, loader);
    return;
  }

  // get absolute library path
  std::string ld_library_path = std::getenv("LD_LIBRARY_PATH");
  std::vector<std::string> library_prefixes;
  std::string delimiter = ":";

  size_t pos = 0;
  std::string lib_path;
  while ((pos = ld_library_path.find(delimiter)) != std::string::npos)
  {
      lib_path = ld_library_path.substr(0, pos);
      library_prefixes.push_back(lib_path);
      ld_library_path.erase(0, pos + delimiter.length());
  }
  library_prefixes.push_back(ld_library_path);

  library_prefixes.push_back("/usr/lib");
  library_prefixes.push_back("/usr/local/lib");


  Poco::SharedLibrary * library_handle = nullptr;

  bool found=false;

  std::string error_message;
  for (const std::string& lp: library_prefixes)
  {
    std::string abs_path=lp+"/"+library_path;

    try {
      setCurrentlyActiveClassLoader(loader);
      setCurrentlyLoadingLibraryName(abs_path);

      // Try to resolve symlinks in the library path, placed here just before load such that all current caching
      // logic etc still works, the actual poco object's path doesn't seem to be used anywhere.
      boost::system::error_code error_code;
      const auto absolute_library_path = boost::filesystem::canonical(abs_path, error_code);
      auto library_path_to_load = abs_path;
      if (error_code.value() == boost::system::errc::success) {
          library_path_to_load = absolute_library_path.string();
      }

      library_handle = new Poco::SharedLibrary(library_path_to_load);
      found=true;
      break;
    } catch (const Poco::LibraryLoadException & e) {
      setCurrentlyLoadingLibraryName("");
      setCurrentlyActiveClassLoader(nullptr);
      error_message+="Could not load library "+abs_path+ " (Poco exception =  " + std::string(e.message()) + ")\n";
    } catch (const Poco::LibraryAlreadyLoadedException & e) {
      setCurrentlyLoadingLibraryName("");
      setCurrentlyActiveClassLoader(nullptr);
      error_message+="Library already loaded "+abs_path+ " (Poco exception =  " + std::string(e.message()) + ")\n";
    } catch (const Poco::NotFoundException & e) {
      setCurrentlyLoadingLibraryName("");
      setCurrentlyActiveClassLoader(nullptr);
      error_message+="Library not found "+abs_path+ " (Poco exception =  " + std::string(e.message()) + ")\n";
    }


    setCurrentlyLoadingLibraryName("");
    setCurrentlyActiveClassLoader(nullptr);
  }

  if (!found)
  {
    throw cnr_class_loader::LibraryLoadException(error_message);
  }

  assert(library_handle != nullptr);
  CONSOLE_BRIDGE_logDebug(
    "class_loader.impl: "
    "Successfully loaded library %s into memory (Poco::SharedLibrary handle = %p).",
    library_path.c_str(), reinterpret_cast<void *>(library_handle));

  // Graveyard scenario
  size_t num_lib_objs = allMetaObjectsForLibrary(library_path).size();
  if (0 == num_lib_objs) {
    CONSOLE_BRIDGE_logDebug(
      "class_loader.impl: "
      "Though the library %s was just loaded, it seems no factory metaobjects were registered. "
      "Checking factory graveyard for previously loaded metaobjects...",
      library_path.c_str());
    revivePreviouslyCreateMetaobjectsFromGraveyard(library_path, loader);
    // Note: The 'false' indicates we don't want to invoke delete on the metaobject
    purgeGraveyardOfMetaobjects(library_path, loader, false);
  } else {
    CONSOLE_BRIDGE_logDebug(
      "class_loader.impl: "
      "Library %s generated new factory metaobjects on load. "
      "Destroying graveyarded objects from previous loads...",
      library_path.c_str());
    purgeGraveyardOfMetaobjects(library_path, loader, true);
  }

  // Insert library into global loaded library vector
  boost::recursive_mutex::scoped_lock llv_lock(getLoadedLibraryVectorMutex());
  LibraryVector & open_libraries = getLoadedLibraryVector();
  // Note: Poco::SharedLibrary automatically calls load() when library passed to constructor
  open_libraries.push_back(LibraryPair(library_path, library_handle));
}

void unloadLibrary(const std::string & library_path, ClassLoader * loader)
{
  if (hasANonPurePluginLibraryBeenOpened()) {
    CONSOLE_BRIDGE_logDebug(
      "class_loader.impl: "
      "Cannot unload %s or ANY other library as a non-pure plugin library was opened. "
      "As class_loader has no idea which libraries class factories were exported from, "
      "it can safely close any library without potentially unlinking symbols that are still "
      "actively being used. "
      "You must refactor your plugin libraries to be made exclusively of plugins "
      "in order for this error to stop happening.",
      library_path.c_str());
  } else {
    CONSOLE_BRIDGE_logDebug(
      "class_loader.impl: "
      "Unloading library %s on behalf of ClassLoader %p...",
      library_path.c_str(), reinterpret_cast<void *>(loader));
    boost::recursive_mutex::scoped_lock lock(getLoadedLibraryVectorMutex());
    LibraryVector & open_libraries = getLoadedLibraryVector();
    LibraryVector::iterator itr = findLoadedLibrary(library_path);
    if (itr != open_libraries.end()) {
      Poco::SharedLibrary * library = itr->second;
      std::string library_path = itr->first;
      try {
        destroyMetaObjectsForLibrary(library_path, loader);

        // Remove from loaded library list as well if no more factories associated with said library
        if (!areThereAnyExistingMetaObjectsForLibrary(library_path)) {
          CONSOLE_BRIDGE_logDebug(
            "class_loader.impl: "
            "There are no more MetaObjects left for %s so unloading library and "
            "removing from loaded library vector.\n",
            library_path.c_str());
          library->unload();
          assert(library->isLoaded() == false);
          delete (library);
          itr = open_libraries.erase(itr);
        } else {
          CONSOLE_BRIDGE_logDebug(
            "class_loader.impl: "
            "MetaObjects still remain in memory meaning other ClassLoaders are still using library"
            ", keeping library %s open.",
            library_path.c_str());
        }
        return;
      } catch (const Poco::RuntimeException & e) {
        delete (library);
        throw cnr_class_loader::LibraryUnloadException(
                "Caught a poco exception while unloading library " + library_path + ": " + e.message());
      }
    }
    throw cnr_class_loader::LibraryUnloadException(
            "Attempt to unload library that class_loader is unaware of: " + library_path);
  }
}


// Other

void printDebugInfoToScreen()
{
  printf("*******************************************************************************\n");
  printf("*****                 class_loader impl DEBUG INFORMATION                 *****\n");
  printf("*******************************************************************************\n");

  printf("OPEN LIBRARIES IN MEMORY:\n");
  printf("--------------------------------------------------------------------------------\n");
  boost::recursive_mutex::scoped_lock lock(getLoadedLibraryVectorMutex());
  LibraryVector libs = getLoadedLibraryVector();
  for (size_t c = 0; c < libs.size(); c++) {
    printf(
      "Open library %zu = %s (Poco SharedLibrary handle = %p)\n",
      c, (libs.at(c)).first.c_str(), reinterpret_cast<void *>((libs.at(c)).second));
  }

  printf("METAOBJECTS (i.e. FACTORIES) IN MEMORY:\n");
  printf("--------------------------------------------------------------------------------\n");
  MetaObjectVector meta_objs = allMetaObjects();
  for (size_t c = 0; c < meta_objs.size(); c++) {
    AbstractMetaObjectBase * obj = meta_objs.at(c);
    printf("Metaobject %zu (ptr = %p):\n TypeId = %s\n Associated Library = %s\n",
      c,
      reinterpret_cast<void *>(obj),
      (typeid(*obj).name()),
      obj->getAssociatedLibraryPath().c_str());

    ClassLoaderVector loaders = obj->getAssociatedClassLoaders();
    for (size_t i = 0; i < loaders.size(); i++) {
      printf(" Associated Loader %zu = %p\n", i, reinterpret_cast<void *>(loaders.at(i)));
    }
    printf("--------------------------------------------------------------------------------\n");
  }

  printf("********************************** END DEBUG **********************************\n");
  printf("*******************************************************************************\n\n");
}


}  // namespace impl
}  // namespace cnr_class_loader
