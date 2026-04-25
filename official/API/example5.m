%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% This example shows how to generate and save a sequence of vocal tract
% contour files (TXT format) from a gestural score.
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
% int vtlGesturalScoreToTractContourFiles(const char* gesFileName,
%   const char* outputFolder);
% *****************************************************************************

outputFolder = 'c:\\temp\\';
gesFileName = 'example5-input.ges';
failed = ...
  calllib(libName, 'vtlGesturalScoreToTractContourFiles', gesFileName, outputFolder)

% Plot a couple of the exported vocal tract contours.

A1 = readmatrix([outputFolder 'contour0000.txt'], 'NumHeaderLines', 1, 'Delimiter', ' ');
A2 = readmatrix([outputFolder 'contour0010.txt'], 'NumHeaderLines', 1, 'Delimiter', ' ');
A3 = readmatrix([outputFolder 'contour0020.txt'], 'NumHeaderLines', 1, 'Delimiter', ' ');
A4 = readmatrix([outputFolder 'contour0030.txt'], 'NumHeaderLines', 1, 'Delimiter', ' ');

plot(A1(:, 1), A1(:, 2), 'r', ...
    A2(:, 1), A2(:, 2), 'g', ...
    A3(:, 1), A3(:, 2), 'b', ...
    A4(:, 1), A4(:, 2), 'k');
axis equal;

% *****************************************************************************
% Close the VTL synthesis.
%
% void vtlClose();
% *****************************************************************************

calllib(libName, 'vtlClose');

unloadlibrary(libName);

