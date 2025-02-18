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

#include "cnr_class_loader/multi_library_class_loader.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace cnr_class_loader
{

MultiLibraryClassLoader::MultiLibraryClassLoader(bool enable_ondemand_loadunload)
: enable_ondemand_loadunload_(enable_ondemand_loadunload)
{
}

MultiLibraryClassLoader::~MultiLibraryClassLoader()
{
  shutdownAllClassLoaders();
}

std::vector<std::string> MultiLibraryClassLoader::getRegisteredLibraries()
{
  std::vector<std::string> libraries;
  for (auto & it : active_class_loaders_) {
    if (it.second != nullptr) {
      libraries.push_back(it.first);
    }
  }
  return libraries;
}

ClassLoader * MultiLibraryClassLoader::getClassLoaderForLibrary(const std::string & library_path)
{
  LibraryToClassLoaderMap::iterator itr = active_class_loaders_.find(library_path);
  if (itr != active_class_loaders_.end()) {
    return itr->second;
  } else {return nullptr;}
}

ClassLoaderVector MultiLibraryClassLoader::getAllAvailableClassLoaders()
{
  ClassLoaderVector loaders;
  for (auto & it : active_class_loaders_) {
    loaders.push_back(it.second);
  }
  return loaders;
}

bool MultiLibraryClassLoader::isLibraryAvailable(const std::string & library_name)
{
  return getClassLoaderForLibrary(library_name) != nullptr;
}

void MultiLibraryClassLoader::loadLibrary(const std::string & library_path)
{


  if (!isLibraryAvailable(library_path)) {
    active_class_loaders_[library_path] =
      new cnr_class_loader::ClassLoader(library_path, isOnDemandLoadUnloadEnabled());
  }
}

void MultiLibraryClassLoader::shutdownAllClassLoaders()
{
  std::vector<std::string> available_libraries = getRegisteredLibraries();

  for (auto & library_path : getRegisteredLibraries()) {
    unloadLibrary(library_path);
  }
}

int MultiLibraryClassLoader::unloadLibrary(const std::string & library_path)
{
  int remaining_unloads = 0;
  LibraryToClassLoaderMap::iterator itr = active_class_loaders_.find(library_path);
  if (itr != active_class_loaders_.end()) {
    ClassLoader * loader = itr->second;
    if (0 == (remaining_unloads = loader->unloadLibrary())) {
      delete (loader);
      active_class_loaders_.erase(itr);
    }
  }
  return remaining_unloads;
}

}  // namespace cnr_class_loader
