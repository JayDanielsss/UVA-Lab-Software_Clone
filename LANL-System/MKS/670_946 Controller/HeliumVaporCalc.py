
'''
Created on 9 Apr 2015

@author: klein
'''
import ROOT as RO    # gives us root
import math          # gives us access to math
import array         # using arrays
import numpy as np   # we will need numpy for the slpine for low temperatures
from scipy import interpolate   # see above
#import matplotlib.pyplot as plt
import sys
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
        
        RO.gROOT.Reset()
        
        self.myC = RO.TCanvas("Helium liquid vapor pressure")
         
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

        
    def Graph(self,npoints,P_low,P_high,units):
        """
        Plots T vs P for Helium using CalT, units is either Torr or Pa
        """
        ulimit = .0009  # lower limit in Torr
        hlimit = 1471. # high limit in Torr
        cvert = 133.322  # converts Torr in Pa
        if(units == 'Pa'):
            if(P_low <  ulimit*cvert or P_high > hlimit * cvert):
                print "invalid range "
                sys.exit()
        else:
            if(P_low <  ulimit or P_high > hlimit ):
                print "invalid range "
                sys.exit()
           
                
        
        
        
        # determine the pressure pointdelta
        stepTorr = (P_high-P_low)/npoints
        stepPa = (P_high-P_low)/npoints/cvert
        
        # initialize arrays, empty. we append then
        Temp = array.array('d',[])
        Pressure = array.array('d',[])
        if units == 'Pa':
            #print units
            Pstart = P_low /cvert
            for k in range(npoints):
                Pressure.append(Pstart+k*stepPa)
                Temp.append(self.CalT(Pressure[k]))
                Pressure[k] = Pressure[k]*cvert  # going back to Pascal
 #               print Temp[k],'  ',Pressure[k], '  ',Pressure[k]*cvert
        else:
            Pstart = P_low
            for k in range(npoints):
                Pressure.append(Pstart+k*stepTorr)
                Temp.append(self.CalT(Pressure[k]))
        
        # now we create the Graph
        #self.myC.cd()
        self.myC.cd()
        self.myC.SetGrid()
        self.myC.SetFillColor(42)
        self.myGraph = RO.TGraph(npoints,Temp,Pressure)
        title_string = "He Vapor Pressure vs Temperature in "+units
        self.myGraph.SetTitle(title_string)
        self.myGraph.SetLineColor(2)
        self.myGraph.SetLineWidth(3)
        self.myGraph.Draw()
        
        self.myC.Update()
        
        return self.myGraph
    def GraphT(self):   
        """ graph of P vs T
        It plots values from .5K to 1.25 K in .01K steps
        """
        npoints = 75
        Temp = array.array('d',[])
        Pressure = array.array('d',[])
        Pola = array.array('d',[]) # unnormalized polarziation tanh(B/T);
        Temp_start = .5
        TempDelta = .01
        #gyro = 28024.95266 #MHz/T
        gyro = 1.
        k_A = 1.38064852e-23 #J/K
        muB = 9.27400968e-24  # J/K
        Bfield = 5.
        for k in range(75):
            Temp.append(Temp_start + k*TempDelta)
            Pressure.append(self.CalculateP(Temp[k]))
            Pola.append(math.tanh(Bfield*muB*gyro/(2.*k_A*(Temp_start+k*TempDelta))))
            print Temp[k],'   ',Pressure[k]
        
        self.myGraph2 = RO.TGraph(npoints,Temp,Pressure)
        self.myGraph3 = RO.TGraph(npoints,Temp,Pola)
        self.myGraph2.SetLineColor(3)
        self.myGraph2.SetLineWidth(3)
        self.myC.Divide(1,2)
        self.myC.cd(1)
        self.myGraph2.Draw()
        self.myC.cd(2)
        self.myGraph3.Draw()
       
        self.myC.Update()
       

    def MakeF(self):
        
        self.f1 = RO.TF1("sinx","sin(x)",0.,10.)
        self.f1.Draw()
        self.myC.Update()
            
                
            
     
     
if __name__ == '__main__':
    mycal = HeVapTemp()
 #   mycal.CalT(.250991)
    #mycal.MakeF()
    
    mycal.Graph(100,.05,1.,'Torr')
    #mycal.Graph(100,.12,133.322,'Pa')
    #print mycal.CalculateP(1.000)
    #mycal.GraphT()
    
    # c.Update()
    raw_input('continue')
       
    
    #pass


