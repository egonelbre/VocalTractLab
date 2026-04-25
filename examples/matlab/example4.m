%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% This example generates the transition from /O/ to /e/ (for the diphthong /OY/)
% using the incremental tube synthesis with the function 
% vtlSynthesisAddTube(...).
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

% File name of the dll and header file (they differ only in the extension)

clear;
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
% Get information about the parameters of the vocal tract model and the
% glottis model.
%
% void vtlGetTractParamInfo(char *names, double *paramMin, double *paramMax, 
%   double *paramNeutral);
% void vtlGetGlottisParamInfo(char *names, double *paramMin, double *paramMax, 
%   double *paramNeutral);
% *****************************************************************************

% Reserve 32 chars for each parameter.
tractParamNames = blanks(numVocalTractParams*32);
tractParamMin = zeros(1, numVocalTractParams);
tractParamMax = zeros(1, numVocalTractParams);
tractParamNeutral = zeros(1, numVocalTractParams);

[failure, tractParamNames, tractParamMin, tractParamMax, tractParamNeutral] = ...
  calllib(libName, 'vtlGetTractParamInfo', tractParamNames, tractParamMin, ...
  tractParamMax, tractParamNeutral);
    
% Reserve 32 chars for each parameter.
glottisParamNames = blanks(numGlottisParams*32);
glottisParamMin = zeros(1, numGlottisParams);
glottisParamMax = zeros(1, numGlottisParams);
glottisParamNeutral = zeros(1, numGlottisParams);

[failure, glottisParamNames, glottisParamMin, glottisParamMax, glottisParamNeutral] = ...
  calllib(libName, 'vtlGetGlottisParamInfo', glottisParamNames, glottisParamMin, ...
  glottisParamMax, glottisParamNeutral);

disp(['Vocal tract parameters: ' tractParamNames]);
disp(['Glottis parameters: ' glottisParamNames]);

% *****************************************************************************
% Get the vocal tract parameter values for the vocal tract shapes of /O/
% and /e/, which are saved in the speaker file.
%
% int vtlGetTractParams(char *shapeName, double *param);
% *****************************************************************************

shapeName = 'O';
paramsO = zeros(1, numVocalTractParams);
[failed, shapeName, paramsO] = ...
  calllib(libName, 'vtlGetTractParams', shapeName, paramsO);

if (failed ~= 0)
    disp('Error: Vocal tract shape "O" not in the speaker file!');   
    return;
end

shapeName = 'e';
paramsE = zeros(1, numVocalTractParams);
[failed, shapeName, paramsE] = ...
  calllib(libName, 'vtlGetTractParams', shapeName, paramsE);

if (failed ~= 0)
    disp('Error: Vocal tract shape "e" not in the speaker file!');   
    return;
end

% *******************************************************************
% Get the tube definitions for the two vowels.
% *******************************************************************

tubeLengthO_cm = zeros(1, numTubeSections);
tubeAreaO_cm2 = zeros(1, numTubeSections);
tubeArticulatorO = zeros(1, numTubeSections, 'int32');

tubeLengthE_cm = zeros(1, numTubeSections);
tubeAreaE_cm2 = zeros(1, numTubeSections);
tubeArticulatorE = zeros(1, numTubeSections, 'int32');

% These extra parameters are assumed to be equal for both vowels.
incisorPos_cm = [ 0.0 ];
tongueTipSideElevation = [ 0.0 ];
velumOpening_cm2 = [ 0.0 ];

[failed, paramsO, tubeLengthO_cm, tubeAreaO_cm2, ...
  tubeArticulatorO, incisorPos_cm, tongueTipSideElevation, velumOpening_cm2] = ...
  calllib(libName, 'vtlTractToTube', paramsO, tubeLengthO_cm, tubeAreaO_cm2, ...
  tubeArticulatorO, incisorPos_cm, tongueTipSideElevation, velumOpening_cm2);

[failed, paramsE, tubeLengthE_cm, tubeAreaE_cm2, ...
  tubeArticulatorE, incisorPos_cm, tongueTipSideElevation, velumOpening_cm2] = ...
  calllib(libName, 'vtlTractToTube', paramsE, tubeLengthE_cm, tubeAreaE_cm2, ...
  tubeArticulatorE, incisorPos_cm, tongueTipSideElevation, velumOpening_cm2);

% *****************************************************************************
% Incrementally synthesize a transition from /O/ to /e/.
%
% void vtlResetTubeSynthesis(char* glottisName, double* staticGlottisParams,
%  double tracheaLength_cm, double noseLength_cm,
%  double piriformFossaLength_cm, double piriformFossaVolume_cm3)
%
% int vtlSynthesisAddTube(int numNewSamples, double *audio,
%  double *tubeLength_cm, double *tubeArea_cm2, int *tubeArticulator,
%  double incisorPos_cm, double velumOpening_cm2, double tongueTipSideElevation,
%  double *newGlottisParams);
% *****************************************************************************

audio1 = zeros(1, 1000);
audio2 = zeros(1, 20000);
audio3 = zeros(1, 5000);

glottisParams = glottisParamNeutral;

% Initialize the tube synthesis.

staticGlottisParams = [ 0.4500 1.6000 120.0000 0.4000 ];
tracheaLength_cm = 23.0000;
noseLength_cm = 11.4000;
piriformFossaLength_cm = 2.5000;
piriformFossaVolume_cm3 = 1.5000;

calllib(libName, 'vtlResetTubeSynthesis', 'Geometric glottis', ...
    staticGlottisParams, tracheaLength_cm, noseLength_cm, piriformFossaLength_cm, piriformFossaVolume_cm3);

% Submit the initial tube shape (numSamples=0) with P_sub = 0

glottisParams(2) = 0;       % P_sub = 0 dPa
[failure, audio1] = ...
  calllib(libName, 'vtlSynthesisAddTube', 0, audio1, ...
    tubeLengthO_cm, tubeAreaO_cm2, tubeArticulatorO, ...
    incisorPos_cm, tongueTipSideElevation, velumOpening_cm2, ...
    glottisParams);

% Phase 1: Ramp up the subglottal pressure within 1000 samples

glottisParams(1) = 140;       % Hz
glottisParams(2) = 8000;       % P_sub = 8000 dPa
[failure, audio1] = ...
  calllib(libName, 'vtlSynthesisAddTube', length(audio1), audio1, ...
    tubeLengthO_cm, tubeAreaO_cm2, tubeArticulatorO, ...
    incisorPos_cm, tongueTipSideElevation, velumOpening_cm2, ...
    glottisParams);

% Phase 2: Make the transition to /e/.

glottisParams(1) = 90;       % Hz
glottisParams(2) = 8000;       % P_sub = 8000 dPa
[failure, audio2] = ...
  calllib(libName, 'vtlSynthesisAddTube', length(audio2), audio2, ...
    tubeLengthE_cm, tubeAreaE_cm2, tubeArticulatorE, ...
    incisorPos_cm, tongueTipSideElevation, velumOpening_cm2, ...
    glottisParams);

% Phase 3: Fall off with the lung pressure.

glottisParams(2) = 0;       % P_sub = 0 dPa
[failure, audio3] = ...
  calllib(libName, 'vtlSynthesisAddTube', length(audio3), audio3, ...
    tubeLengthE_cm, tubeAreaE_cm2, tubeArticulatorE, ...
    incisorPos_cm, tongueTipSideElevation, velumOpening_cm2, ...
    glottisParams);

% Concatenate the audio segments.

audio = [audio1 audio2 audio3];

% Plot and play the audio signal

plot(audio);
soundsc(audio, double(audioSamplingRate));
audiowrite('example4-output.wav', audio, audioSamplingRate);

% *****************************************************************************
% Close the VTL synthesis.
%
% void vtlClose();
% *****************************************************************************

calllib(libName, 'vtlClose');

unloadlibrary(libName);
