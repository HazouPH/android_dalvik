/*
 * Copyright (C) 2013 The Android Open Source Project
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
 * Covers testing for String.regionMatches.
 */
public class RegionMatchesTest {
    /*
     * Flag whether to dump result of regionMatches
     */
    static boolean printResult = false;

    /*
     * Number of iterations for testing loop
     */
    static int ITERATIONS = 100000;

    /*
     * Strings have the same value
     */
    static void testStringSame() {
        // Strings are exactly the same
        String s1 = new String("The stock symbol for Intel is INTC");
        String s2 = new String("The stock symbol for Intel is INTC");
        boolean result1 = s1.regionMatches(0, s2, 0, 34);
        boolean result2 = s2.regionMatches(10, s1, 10, 10);
        boolean result3 = s1.regionMatches(5, s2, 11, 10);

        if(printResult) {
            System.out.println(result1);
            System.out.println(result2);
            System.out.println(result3);
        }
    }

    /*
     * Strings have the same length.
     */
    static void testStringSameLength() {
        // Strings are same length
        String s1 = new String("The stock symbol for Intel is INTC ");
        String s2 = new String("The stock symbol for Google is GOOG");
        boolean result = s2.regionMatches(0, s1, 0, 34);

        if(printResult) {
            System.out.println(result);
        }
    }

    /*
     * Strings have different lengths.
     */
    static void testStringDifferentLength() {
        // Strings are different length
        String s1 = new String("The stock symbol for Intel is INTC");
        String s2 = new String("The stock symbol for Google is GOOG");
        boolean result = s2.regionMatches(0, s1, 0, 34);

        if(printResult) {
            System.out.println(result);
        }
    }

    /*
     * Only other string is null.
     */
    static void testNullStringOther() {
        String s1 = new String("INTC");
        String s2 = null;
        boolean result = s1.regionMatches(0, s2, 0, 4);
    }

    /*
     * This string is null.
     */
    static void testNullStringThis() {
        String s2 = new String("INTC");
        String s1 = null;
        boolean result2 = s1.regionMatches(0, s2, 0, 4);
    }

    /*
     * A zero length region is tested.
     */
    static void testZeroLength() {
        String s1 = new String("INTC");
        String s2 = new String("INTC");
        boolean result = s1.regionMatches(0, s2, 1, 0);

        if(printResult) {
            System.out.println(result);
        }
    }

    /*
     * The strings are the same reference.
     */
    static void testStringSameReference() {
        // Strings are same reference
        String s1 = new String("INTC");
        String s2 = s1;

        boolean result1 = s1.regionMatches(0, s2, 0, 4);
        boolean result2 = s1.regionMatches(0, s2, 1, 3);

        if(printResult) {
            System.out.println(result1);
            System.out.println(result2);
        }
    }

    /*
     * regionMatches parameters are bad and should return false.
     */
    static void testStringBadParameters() {
        String s1 = new String("INTC");
        String s2 = new String("INTC");
        // bad toffset
        boolean result1 = s1.regionMatches(-2, s2, 0, 3);
        // bad ooffset
        boolean result2 = s1.regionMatches(0, s2, -2, 3);
        // bad toffset + len
        boolean result3 = s1.regionMatches(2, s2, 0, 3);
        // bad ooffset + len
        boolean result4 = s1.regionMatches(0, s2, 3, 3);

        if(printResult) {
            System.out.println(result1);
            System.out.println(result2);
            System.out.println(result3);
            System.out.println(result4);
        }
    }

    /*
     * One of the "strings" is actually an Object.
     */
    static void testNoString() {
        String s1 = new String("INTC");
        Object s2 = new Object();
        boolean result = s1.regionMatches(3, (String)s2, 4, 10);

        if(printResult) {
            System.out.println(result);
        }
    }

    /*
     * Start the test harness for regionMatches
     */
    static public void runTest() {
        System.out.println("Starting regionMatches test");

        boolean tmpResult;

        // Trying to make regionMatches hot by using it many times.
        for(int i = 0; i < ITERATIONS; i++) {
            testStringSame();
            testStringSameLength();
            testStringDifferentLength();
            testStringSameReference();
            testStringBadParameters();
            testZeroLength();
        }

        // Now print the actual results of the region matching
        printResult = true;
        testStringSame();
        testStringSameLength();
        testStringDifferentLength();
        testStringSameReference();
        testStringBadParameters();
        testZeroLength();
        try {
            testNullStringOther();
        } catch (Exception e) {
            System.out.println(e.toString());
        }
        try {
            testNullStringThis();
        } catch (Exception e) {
            System.out.println(e.toString());
        }
        try {
            testNoString();
        } catch (Exception e) {
            System.out.println(e.toString());
        }
    }
}
