import java.io.*;
class Main{
static int cmplong(long a, long b){
 if (a<b)
   return 20;
 else if(a>b)
   return 21;
 else
   return 22;
}
public static void main (String[] args)
{
 long a=0,b=0,c1=0,c2=0,c3=0,c4=0,c5=0;
 int d=0;
 a=1<<50;
 b=1<<51;
 for (int i=0;i<70000;i++)
 {
 d=cmplong(a,b);
   if (d != 20)
      System.out.println("error in less than" +"\n" );
   else
      c1=c1+500;
 }
 System.out.println("c1 = " + c1 + "\n" );
 a=1<<51;
 b=1<<50;
 for (int i=0;i<70000;i++)
 {
 d=cmplong(a,b);
   if (d!=21)
      System.out.println("error in greater than" +"\n" );
   else
      c2=c2+500;
 }
 System.out.println("c2 = " + c2 + "\n" );
 a=30;
 b=40;
 for (int i=0;i<70000;i++)
 {
 d=cmplong(a,b);
   if (d!=20)
      System.out.println("error in less than w/o shift" +"\n" );
   else
      c3=c3+500;
 }
 System.out.println("c3 = " + c3 + "\n" );
 a=40;
 b=40;
 for (int i=0;i<70000;i++)
 {
 d=cmplong(a,b);
   if (d!=22)
      System.out.println("equal error" +"\n" );
   else
      c4=c4+500;
 }
 System.out.println("c4 = " + c4 + "\n" );
 a=40;
 b=30;
 for (int i=0;i<70000;i++)
 {
 d=cmplong(a,b);
   if (d!=21)
      System.out.println("greater than w/o shift error" +"\n" );
   else
      c5=c5+500;
 }
 System.out.println("c5 = " + c5 + "\n" );
}
}
