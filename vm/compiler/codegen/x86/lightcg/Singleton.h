/*
 * Copyright (C) 2010-2013 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file Singleton.h
 * @brief Implements the interface for singleton pattern.
 */

/**
 * @details Calling this function allows any non-singleton class to be treated
 * as a singleton. However, please note that making a copy of the instance
 * is still allowed and your new copy is no longer the singleton instance.
 * @return singleton instance
 */
template <class T>
T& singleton() {
    // Better not to use dynamic allocation so that we can make sure
    // that it will be destroyed.
    static T instance;
    return instance;
}

/**
 * @details Calling this function allows any non-singleton class to be treated
 * as a singleton.
 * @return pointer to singleton instance
 * @warning This is not thread safe. It also does not call class destructor
 * and leaks on program exit. Calls to this should not be mixed with calls to
 * singleton<T>() because they will not retrieve the same instance.
 */
template <class T>
T* singletonPtr() {
    static T * instance = 0;

    // Create a new instance if one doesn't already exist
    if (instance == 0)
        instance = new T();

    return instance;
}
