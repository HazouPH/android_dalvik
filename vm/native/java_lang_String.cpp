/*
 * Copyright (C) 2008 The Android Open Source Project
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

/*
 * java.lang.String
 */
#include "Dalvik.h"
#include "native/InternalNativePriv.h"

#ifdef HAVE__MEMCMP16
/* hand-coded assembly implementation, available on some platforms */
/* "count" is in 16-bit units */
extern "C" u4 __memcmp16(const u2* s0, const u2* s1, size_t count);
#endif

static void String_charAt(const u4* args, JValue* pResult)
{
    MAKE_INTRINSIC_TRAMPOLINE(javaLangString_charAt);
}

static void String_compareTo(const u4* args, JValue* pResult)
{
    MAKE_INTRINSIC_TRAMPOLINE(javaLangString_compareTo);
}

static void String_equals(const u4* args, JValue* pResult)
{
    MAKE_INTRINSIC_TRAMPOLINE(javaLangString_equals);
}

static void String_fastIndexOf(const u4* args, JValue* pResult)
{
    MAKE_INTRINSIC_TRAMPOLINE(javaLangString_fastIndexOf_II);
}

static void String_intern(const u4* args, JValue* pResult)
{
    StringObject* str = (StringObject*) args[0];
    StringObject* interned = dvmLookupInternedString(str);
    RETURN_PTR(interned);
}

static void String_isEmpty(const u4* args, JValue* pResult)
{
    MAKE_INTRINSIC_TRAMPOLINE(javaLangString_isEmpty);
}

static void String_length(const u4* args, JValue* pResult)
{
    MAKE_INTRINSIC_TRAMPOLINE(javaLangString_length);
}

/**
 * @brief Native implementation for String\.regionMatches
 * @details public boolean regionMatches(int toffset, String other,
 * int ooffset, int len)
 * @param args arguments to regionMatches method \n
 * args[0]: this - self String \n
 * args[1]: thisStart - starting offset in this string \n
 * args[2]: string - other String \n
 * args[3]: start - starting offset in the other string \n
 * args[4]: length - number of characters to compare
 * @param pResult - result of the region comparison
*/
static void String_regionMatches(const u4* args, JValue* pResult)
{
    int thisCount, thisOffset, otherCount, otherOffset;
    ArrayObject * thisArray;
    ArrayObject * otherArray;
    // Use u2 for the arrays so we can compare on Java 16-bit char at a time
    const u2 * thisChars;
    const u2 * otherChars;

    // Initialize the arguments passed to the regionMatches method
    Object * thisString = reinterpret_cast<Object *>(args[0]);
    int thisStart = static_cast<int>(args[1]);
    Object * otherString = reinterpret_cast<Object *>(args[2]);
    int otherStart = static_cast<int>(args[3]);
    int length = static_cast<int>(args[4]);

    // Check if this and the other string are null
    if (thisString == NULL || otherString == NULL) {
        // Technically thisString should never be null when we get here but
        // we should handle it instead of crashing later in this function

        const char * message = NULL;

        // When the otherString is null, we need to include a message
        if (otherString == NULL) {
            message = "string == null";
        }

        dvmThrowNullPointerException(message);
        RETURN_VOID();
    }

    // Initialize some of the locals with the fields from the String object
    thisCount = dvmGetFieldInt(thisString, STRING_FIELDOFF_COUNT);
    otherCount = dvmGetFieldInt(otherString, STRING_FIELDOFF_COUNT);

    // Check that bounds are correct
    if ((otherStart < 0) || (thisStart < 0) || (length > thisCount - thisStart)
            || (length > otherCount - otherStart)) {
        RETURN_BOOLEAN(false);
    }

    // If length is 0 or less, there is nothing to compare so region matches
    if (length <= 0) {
        RETURN_BOOLEAN(true);
    }

    // Finish initializing the locals with the fields from the String object
    thisOffset = dvmGetFieldInt(thisString, STRING_FIELDOFF_OFFSET);
    otherOffset = dvmGetFieldInt(otherString, STRING_FIELDOFF_OFFSET);
    thisArray = reinterpret_cast<ArrayObject *>(dvmGetFieldObject(thisString,
            STRING_FIELDOFF_VALUE));
    otherArray = reinterpret_cast<ArrayObject *>(dvmGetFieldObject(otherString,
            STRING_FIELDOFF_VALUE));

    // Point the two arrays to the proper offset to start comparing from
    thisChars = ((const u2*) (void*) thisArray->contents) + thisOffset + thisStart;
    otherChars = ((const u2*) (void*) otherArray->contents) + otherOffset + otherStart;



    // Now compare the characters
#if defined(HAVE__MEMCMP16)
    bool result = (__memcmp16(thisChars, otherChars, length) == 0);
#elif defined(ARCH_IA32)
    // Each Java char is 2 bytes
    int numBytes = length * 2;

    // For x86 we can use the SSE optimized memcmp
    bool result = (memcmp(thisChars, otherChars, numBytes) == 0);
#else
    // Eagerly set regionMatches return value to true unless otherwise proven
    bool result = true;
    for (int i = length - 1; i >= 0; --i) {
        if (thisChars[i] != otherChars[i]) {
            result = false;
            break;
        }
    }
#endif

    // Return result of the comparison
    RETURN_BOOLEAN(result);
}

const DalvikNativeMethod dvm_java_lang_String[] = {
    { "charAt",      "(I)C",                  String_charAt },
    { "compareTo",   "(Ljava/lang/String;)I", String_compareTo },
    { "equals",      "(Ljava/lang/Object;)Z", String_equals },
    { "fastIndexOf", "(II)I",                 String_fastIndexOf },
    { "intern",      "()Ljava/lang/String;",  String_intern },
    { "isEmpty",     "()Z",                   String_isEmpty },
    { "length",      "()I",                   String_length },
    { "regionMatches", "(ILjava/lang/String;II)Z", String_regionMatches },
    { NULL, NULL, NULL },
};
