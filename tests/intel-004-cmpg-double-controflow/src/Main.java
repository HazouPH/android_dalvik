class Main{
    static int cmpgdouble(double a, double b){
        if (a<b)
            return 20;
        else if(a>b)
            return 21;
        else if (a==b)
            return 22;
        else
            return 23;
    }
    public static void main (String[] args){
        double a=0.0D,b=0.0D,c1=0.0D,c2=0.0D,c3=0.0D,c4=0.0D,c5=0.0D;
        int d=0;
        a=30.0D;
        b=40.0D;
            for (int i=0;i<7000000;i++){
                d=cmpgdouble(a,b);
                if (d!=20)
                    System.out.println("error in less" +"\n" );
                else
                    c1=c1+500.0D;
            }
        System.out.println("c1 = " + c1+ "\n" );

        a=40.0D;
        b=40.0D;
            for (int i=0;i<7000000;i++){
                d=cmpgdouble(a,b);
                if (d!=22)
                    System.out.println("equal error" +"\n" );
                else
                    c2=c2+500.0D;
            }
        System.out.println("c2 = " + c2 + "\n" );

        a=40.0D;
        b=30.0D;
            for (int i=0;i<7000000;i++){
                d=cmpgdouble(a,b);
                if (d!=21)
                    System.out.println("greater than error" +"\n" );
                else
                    c3=c3+500.0D;
            }
        System.out.println("c3 = " + c3+ "\n" );

        a=0.0D/0.0D;
        b=30.0D;
            for (int i=0;i<7000000;i++){
                d=cmpgdouble(a,b);
                if (d!=23)
                    System.out.println("NaN error" +"\n" );
                else
                    c4=c4+500.0D;
            }
        System.out.println("c4 = " + c4+ "\n" );

        b=0.0D/0.0D;
        a=30.0D;
            for (int i=0;i<7000000;i++){
                d=cmpgdouble(a,b);
                if (d!=23)
                    System.out.println("NaN error" +"\n" );
                else
                    c5=c5+500.0D;
            }
        System.out.println("c5 = " + c5+ "\n" );

    }
}
