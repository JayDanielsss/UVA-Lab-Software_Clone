### Headers ###

# Event Number
# Commentary
# Q Curve File
# Q Comment
# TEQ File
# TEQ Comment
# Tune File
# FLower
# FUpper
# Peak Amp (V)
# Peak Center (MHz)
# Beam ON
# RF Level (dBm)
# IF Atten (dB)
# Task3 Temperature
# Task3 Pressure
# NMR Channel
# ADC_1
# ADC_2
# ... (Up to 400)
# ADC_400

def Write_To_CSV(Save_Path, Commentary, QCurveFile, QComment, TEQFile, TEQComment, TuneFile, 
                 FLower, FUpper, PeakAmp, PeakCenter, BeamON, RFLevel, IFAtten, 
                 Task3Temperature, Task3Pressure, NMRChannel, sigArray):
    """
    Writes data to a CSV file in a format compatible with LabVIEW.
    If the file exists, appends the new data. If not, creates a new file.
    
    Parameters:
    -----------
    df : pandas.DataFrame
        Existing DataFrame to append to
    Save_Path : str
        Path where the CSV file will be saved
    Commentary : str
        User commentary for the event
    QCurveFile : str
        Path to Q curve file
    QComment : str
        Comment for Q curve
    TEQFile : str
        Path to TEQ file
    TEQComment : str
        Comment for TEQ file
    TuneFile : str
        Path to tune file
    FLower : float
        Lower frequency bound
    FUpper : float
        Upper frequency bound
    PeakAmp : float
        Peak amplitude in volts
    PeakCenter : float
        Peak center frequency in MHz
    BeamON : bool
        Whether beam is on
    RFLevel : float
        RF level in dBm
    IFAtten : float
        IF attenuation in dB
    Task3Temperature : float
        Temperature reading
    Task3Pressure : float
        Pressure reading
    NMRChannel : int
        NMR channel number
    sigArray : list
        Array of ADC signal values
    
    Returns:
    --------
    int
        1 if successful, error message if failed
    """
    import csv
    import datetime 
    import os

    try:

        EventNumber = int(datetime.datetime.now().timestamp())

        base_data = [EventNumber, Commentary, QCurveFile, QComment, TEQFile, TEQComment, TuneFile, 
                    FLower, FUpper, PeakAmp, PeakCenter, BeamON, RFLevel, IFAtten, 
                    Task3Temperature, Task3Pressure, NMRChannel]

        base_headers = ["Event Numbers", "Commentary", "Q Curve File", "Q Comment", "TEQ File", "TEQ Comment", "Tune File", 
                    "Flower", "FUpper", "Peak Amp (V)", "Peak Center (MHz)", "Beam ON", "RF Level (dBm)", "IF Atten (dB)", 
                    "Task3 Temperature", "Task3 Pressure", "NMR Channel"]
        
        signal_headers = [f"ADC_{i+1}" for i in range(len(sigArray))]   
        all_headers = base_headers + signal_headers

        directory = os.path.dirname(Save_Path)
        if directory and not os.path.exists(directory):
            os.makedirs(directory)

        file_exists = os.path.exists(Save_Path)

        with open(Save_Path, 'a', newline='') as csvfile:
            writer = csv.writer(csvfile)
            
            if not file_exists:
                writer.writerow(all_headers)
            
            writer.writerow(base_data + sigArray)

        return 1  

    except Exception as e:
        print(f"Error: {e}")
        return str(e)  
