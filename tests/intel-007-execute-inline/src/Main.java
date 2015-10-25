public class Main {
    public static void main(String args[]) {
        for ( int i = 0 ; i <= 360 ; i+=45 )
        {
            double d = i * (Math.PI/180.0);
            System.out.println("Math.sin(" + d + ") = " +  Math.sin(d));
            System.out.println("Math.sinh(" + d + ") = " + Math.sinh(d));
            System.out.println("Math.asin(" + d + ") = " + Math.asin(d));
            System.out.println("Math.cos(" + d + ") = " +  Math.cos(d));
            System.out.println("Math.cosh(" + d + ") = " + Math.cosh(d));
            System.out.println("Math.acos(" + d + ") = " + Math.acos(d));
            System.out.println("Math.tan(" + d + ") = " +  Math.tan(d));
            System.out.println("Math.tanh(" + d + ") = " + Math.tanh(d));
            System.out.println("Math.atan(" + d + ") = " + Math.atan(d));
            System.out.println("Math.atan2(" + d + ", " + d+1.0 + ") = " + Math.atan2(d, d+1.0));
        }
        for ( int j = -3 ; j <=3 ; j++ )
        {
            double e = (double) j;
            System.out.println("Math.cbrt(" + e + ") = " +  Math.cbrt(e));
            System.out.println("Math.log(" + e + ") = " +  Math.log(e));
            System.out.println("Math.log10(" + e + ") = " +  Math.log10(e));
            System.out.println("Math.log1p(" + e + ") = " +  Math.log1p(e));
            System.out.println("Math.exp(" + e + ") = " +  Math.exp(e));
            System.out.println("Math.expm1(" + e + ") = " +  Math.expm1(e));
            System.out.println("Math.pow(" + e + ", " + e+1.0 + ") = " +  Math.pow(e, e+1.0));
            System.out.println("Math.hypot(" + e + ", " + e+1.0 + ") = " +  Math.hypot(e, e+1.0));
        }

        System.out.println("Math.ceil(0.0001) = " +  Math.ceil(0.0001));
        System.out.println("Math.floor(0.0001) = " +  Math.floor(0.0001));
        System.out.println("Math.nextAfter(1.0, 2.0) = " +  Math.nextAfter(1.0, 2.0));
        System.out.println("Math.rint(0.5000001) = " +  Math.rint(0.5000001));

    }
}
