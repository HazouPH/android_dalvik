public class Main {
    static int iterations = 1000;

    // Using these constants instead of e.g. Byte.MIN_VALUE
    // to keep all values as integers, except when wanting to convert
    final static int BYTE_MIN = -128;
    final static int BYTE_MAX = 127;
    final static int SHORT_MIN = -32768;
    final static int SHORT_MAX = 32767;

    public static void main(String args[]) {
        int result = 0;

        result = loopByte();
        System.out.println("Byte: Sum over entire range is " + result);
        result = 0;
        result = loopShort();
        System.out.println("Short: Sum over entire range is " + result);
    }

    // Loop through the entire range of Byte and add all the values
    static int loopByte() {
        int sum = 0;

        for (int counter = 0; counter < (iterations * 256); counter++) {
            for (int step = BYTE_MIN; step <= BYTE_MAX; step++) {
                //insert randon bits, which should go away on conversion
                int scratch = 0xF00BA400 | step;
                // generates the int-to-byte bytecode
                sum += (byte) scratch;
            }
            // There are more numbers on the negative side. Return to 0.
            sum -= BYTE_MIN;
        }
        return sum;
    }

    // Loop through the entire range of Byte and add all the values
    static int loopShort() {
        int sum = 0;

        for (int counter = 0; counter < iterations; counter++) {
            for (int step = SHORT_MIN; step <= SHORT_MAX; step++) {
                //insert randon bits, which should go away on conversion
                int scratch = 0xBADD0000 | step;
                // generates the int-to-short bytecode
                sum += (short) scratch;
            }
            // There are more numbers on the negative side. Return to 0
            sum -= SHORT_MIN;
        }
        return sum;
    }

}
