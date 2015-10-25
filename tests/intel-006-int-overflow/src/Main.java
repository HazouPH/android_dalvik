public class Main {
    final static int LOOP_ITERATIONS = 10000;
    final static int INNER_LOOP_ITERATIONS = 300;

    public static void main(String args[]) {
        int res1 = 0;
        int res2 = 0;
        //Check for underflow
        for (int i = 0; i < LOOP_ITERATIONS; i++) {
            //Start with Integer.MIN_VALUE
            //and Integer.MAX_VALUE
            //Then cross over the fence and
            //see where we land.
            int b = Integer.MIN_VALUE;
            int c = Integer.MAX_VALUE;

            do {
                b--; //Should rollover
                c++; //Should rollover
            }
            while ((b > (Integer.MAX_VALUE - INNER_LOOP_ITERATIONS)) &&
                    (c < (Integer.MIN_VALUE + INNER_LOOP_ITERATIONS)));
            res1 = b;
            res2 = c;
        }
        System.out.println(res1 + "\n" + res2);
    }
}