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

def Write_To_CSV(df, Save_Path, Commentary, QCurveFile, QComment, TEQFile, TEQComment, TuneFile, 
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
    import pandas as pd
    import datetime 
    import os

    try:
        # Create Event Number based on current timestamp
        EventNumber = int(datetime.datetime.now().timestamp())

        # Prepare base data
        base_data = [EventNumber, Commentary, QCurveFile, QComment, TEQFile, TEQComment, TuneFile, 
                    FLower, FUpper, PeakAmp, PeakCenter, BeamON, RFLevel, IFAtten, 
                    Task3Temperature, Task3Pressure, NMRChannel]

        # Define headers
        base_headers = ["Event Numbers", "Commentary", "Q Curve File", "Q Comment", "TEQ File", "TEQ Comment", "Tune File", 
                    "Flower", "FUpper", "Peak Amp (V)", "Peak Center (MHz)", "Beam ON", "RF Level (dBm)", "IF Atten (dB)", 
                    "Task3 Temperature", "Task3 Pressure", "NMR Channel"]
        
        signal_headers = [f"ADC_{i+1}" for i in range(len(sigArray))]   
        all_headers = base_headers + signal_headers

        # Create new row
        new_row = pd.DataFrame([base_data + sigArray], columns=all_headers)
        
        # Check if file exists
        if os.path.exists(Save_Path):
            # If file exists, read it and append new data
            existing_df = pd.read_csv(Save_Path)
            df = pd.concat([existing_df, new_row], ignore_index=True)
        else:
            # If file doesn't exist, use the new row as the DataFrame
            df = new_row
        
        # Save to CSV
        df.to_csv(Save_Path, index=False)

        return 1  # Return 1 if successful (LabVIEW requires a return value)

    except Exception as e:
        print(f"Error: {e}")
        return str(e) 