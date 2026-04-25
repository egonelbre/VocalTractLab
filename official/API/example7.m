%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% This example simply shows how to 
% o transform a segment sequence file into a gestural score file,
% o modify gestures in the gestural score file (pitch, duration),
% o generate audio from the (modified) gestural score file.
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

% File name of the dll and header file (they differ only in the extension).

libName = 'VocalTractLabApi';

if ~libisloaded(libName)
    % To load the library, specify the name of the DLL and the name of the
    % header file. If no file extensions are provided (as below)
    % LOADLIBRARY assumes that the DLL ends with .dll and the header file
    % ends with .h.
    loadlibrary(libName, libName);
    disp(['Loaded library: ' libName]);
    pause(1);
end

if ~libisloaded(libName)
    error(['Failed to load external library: ' libName]);
    success = 0;
    return;
end

% *****************************************************************************
% list the methods
% *****************************************************************************

libfunctions(libName);   

% *****************************************************************************
% Print the version (compile date) of the library.
%
% void vtlGetVersion(char *version);
% *****************************************************************************

% Init the variable version with enough characters for the version string
% to fit in.
version = '                                ';
version = calllib(libName, 'vtlGetVersion', version);

disp(['Compile date of the library: ' version]);

% *****************************************************************************
% Initialize the VTL synthesis with the given speaker file name.
%
% void vtlInitialize(const char *speakerFileName)
% *****************************************************************************

speakerFileName = 'JD2.speaker';

failure = calllib(libName, 'vtlInitialize', speakerFileName);
if (failure ~= 0)
    disp('Error in vtlInitialize()!');   
    return;
end

% *****************************************************************************
% Get some constants.
%
% void vtlGetConstants(int *audioSamplingRate, int *numTubeSections,
%   int *numVocalTractParams, int *numGlottisParams);
% *****************************************************************************

audioSamplingRate = 0;
numTubeSections = 0;
numVocalTractParams = 0;
numGlottisParams = 0;

[failure, audioSamplingRate, numTubeSections, numVocalTractParams, numGlottisParams] = ...
    calllib(libName, 'vtlGetConstants', audioSamplingRate, numTubeSections, numVocalTractParams, numGlottisParams);

disp(['Audio sampling rate = ' num2str(audioSamplingRate)]);
disp(['Num. of tube sections = ' num2str(numTubeSections)]);
disp(['Num. of vocal tract parameters = ' num2str(numVocalTractParams)]);
disp(['Num. of glottis parameters = ' num2str(numGlottisParams)]);

% *****************************************************************************
% int vtlSegmentSequenceToGesturalScore(const char *segFileName, 
%   const char *gesFileName);
% *****************************************************************************

segFileName = 'example7-input.seg';
gesFileName = 'example7-output.ges';
wavFileName = 'example7-output.wav';

failed = ...
  calllib(libName, 'vtlSegmentSequenceToGesturalScore', segFileName, gesFileName);

audio = zeros(10*48000, 1);     % Reserve memory for 10 s
numSamples = 0;

% *****************************************************************************
% int vtlGesScoreChangeF0Offset(const char* inputFileName, 
%   const char* outputFileName, double deltaF0_st);
% *****************************************************************************

failed = ...
  calllib(libName, 'vtlGesScoreChangeF0Offset', gesFileName, gesFileName, -5.0);

% *****************************************************************************
% int vtlGesScoreChangeDuration(const char* inputFileName, 
%   const char* outputFileName, double factor);
% *****************************************************************************

failed = ...
  calllib(libName, 'vtlGesScoreChangeDuration', gesFileName, gesFileName, 1.5);

% *****************************************************************************
% int vtlGesScoreChangeTimeConstants(const char* inputFileName, 
%   const char* outputFileName, double factor);
% *****************************************************************************

failed = ...
  calllib(libName, 'vtlGesScoreChangeTimeConstants', gesFileName, gesFileName, 1.5);

% *****************************************************************************
% int vtlGesturalScoreToAudio(const char* gesFileName, 
%   const char* wavFileName, const char *miscFileName, double* audio, 
%   int* numSamples, int normalizeAudio, int enableConsoleOutput);
% *****************************************************************************

failed = ...
    calllib(libName, 'vtlGesturalScoreToAudio', gesFileName, ...
    wavFileName, '', audio, numSamples, 0, 1);

if (failure ~= 0)
    disp('Error in vtlGesturalScoreToAudio()! Error code:');
    failure
    return;
end

% Play the result.

[y, Fs] = audioread(wavFileName);
soundsc(y,Fs);

% *****************************************************************************
% Close the VTL synthesis.
%
% void vtlClose();
% *****************************************************************************

calllib(libName, 'vtlClose');

unloadlibrary(libName);

