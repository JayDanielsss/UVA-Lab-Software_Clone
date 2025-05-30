#!/usr/bin/env python

import math          # gives us access to math
import array         # using arrays
import numpy as np   # we will need numpy for the slpine for low temperatures
from scipy import interpolate   # see above
#import matplotlib.pyplot as plt
import sys
# we will use a class


# we will use a class
class HeVapTemp:
    """
    HeVapTemp will calculate the Temperature of liquid Helium 4 as a function of the saturated Vapor pressure
    The code follows Journal of Physical and Chemical Ref. Data 27,1217 (1998)
    it contains a fast calculation, which is only good below 1.25K, one which goes from 1.25 to 2.1768 and one from 2.1768 to
    5.0
    Currently the pressure is given in Torr
    """

    def __init__(self):
        """ here we deifne all the constants
        since we will reuse the constants extensively, we will make the global
        we will also use arrays of double to make life easier
        """

        # a coeff for the intermediate t-range
        self.a_low = array.array('d',[
        1.392408,
        0.527153,
        0.166756,
        0.050988,
        0.026514,
        0.001975,
        -.017976,
        0.005409,
        0.013259,
        0.0])
        self.b_low = 5.6
        self.c_low = 2.9

        # here we do the upper T range

        self.a_high = array.array('d',[
        3.146631,
        1.357655,
        0.413923,
        0.091159,
        0.016349,
        0.001826,
        -.004325,
        -.004973,
        0.0,
        0.0])
        self.b_high = 10.3
        self.c_high =  1.9

        # lowest T range
        self.m0 = 59.83
        self.R  =  8.314510
        self.k0 = 12.240

        self.CalculateSplineT()  #setuo the spline for low T

    def ConvertTorrPa(self,Torr):
        """ this converts Torr into Pascal)
        """
        return Torr*133.322

    def ConvertPaTorr(self,Pa):
        """ this converts Pascal into Torr
        """
        return Pa/133.322

    def CalT(self,Press):
        """
        this determines which T range should be used
        based on empirical P input
        if P< .826 Torr = 110.123972 Pa use low
           if P>= .826 Torr = 110.123972 Pa  and P< 37.82Torr = 5042.23804 Pa
        if P>= 37.82Torr = 5042.23804 Pa and P< 1471 Torr = 196116.662 Pa use high
        """

        Pa =self.ConvertTorrPa(Press)
        if (Press < .0009):
            print " Spline or function not valid at such low pressure"
            Temp = -999.


        elif (Press>= .0009 and Press < .826) :

            Temp = self.newT(Pa)

        elif(Press >= .826 and Press < 37.82) :
            Temp = self.CalculateT(self.a_low,self.b_low,self.c_low,Pa)

        elif(Press >= 37.82 and Press < 1471.) :
            Temp = self.CalculateT(self.a_high,self.b_high,self.c_high,Pa)

        else: # default 
            print 'T larger than 5K, calc not valid in that range'
            Temp = -999.

        #print ' the temperature is ' , Temp, ' the pressure is ',Press
        return Temp


    def CalculateT(self,a,b,c,P):
        """
         this routine calculates the actual T
        """
        con = (math.log(P)-b)/c
        Temp = 0.0
        for k in range(9):
            Temp = Temp + a[k]*pow(con,k)
        return Temp

    def CalculateP(self,T):
        """ this routine caclulates the Vapor pressure at a different T below 1.25
        reference as above, return is in Pa
        """
        Lconst = 59.83
#        Iconst = 12.2440 this is the value in the article but it is wrong
        Iconst = 9.941842
        Rconst = 8.314510
        #print Lconst/(Rconst*T), '  ',5./2.*math.log(T)
        return  math.exp(Iconst -Lconst/(Rconst*T) +5./2.*math.log(T)) #the /10 is ak

    def CalculateSplineT(self):
        """ this routine uses a cubic spline from scipy to evaluate the T as a function of P
        The input arrays are from the same paper as cited above
        """
        P_array = np.array([1.101e-01,2.923e-01,6.893e-01,1.475,2.914,5.380,9.381,15.58,24.79,38.02,56.47,81.52,114.7])     #array of indep var
        T_array = np.array([.650,.7,.75,.8,.85,.9,.95,1.0,1.05,1.1,1.15,1.2,1.25])

        #linear spline interpolation
        self.newT = interpolate.interp1d(P_array,T_array,kind=1)

if __name__ == '__main__':
    #Command Line Arguements are stored in list argv
    numArgs = len(sys.argv) - 1
    pressure = 0.
    temperature = 0.

    #Iterate through each element and add to the pressure
    for n in range (1, len(sys.argv)):
        pressure = float(sys.argv[n])

    mycal = HeVapTemp()
    temperature = mycal.CalT(pressure)
    #print mycal.CalculateP(1.000)
    #mycal.GraphT()

    # c.Update()
    #raw_input('continue')


    #pass

    #use the print function to output the pressure
    #print pressure
    print temperature
