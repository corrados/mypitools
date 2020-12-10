
% Drum trigger tests

% using a recording of a Roland snare mesh pad

close all;
pkg load signal

org = audioread("snare.wav");
org = org(:, 1); % just the left channel contains all the data

% figure; plot(20 * log10(abs(x)));

pos_sense = org(730000:980000);
roll      = org(395000:510000);

x = pos_sense;
% x = roll;

x = resample(x, 1, 6);

% figure; plot(20 * log10(abs(x)));

% figure; pwelch(x,[],[],[],[],[],'db');

% low pass filter
a    = fir1(40, 0.1);
xlow = filter(a, 1, x);

% figure; pwelch(xlow,[],[],[],[],[],'db');

% high pass filter
a     = fir1(40, 0.2, "high");
xhigh = filter(a, 1, x);

% figure; pwelch(xhigh,[],[],[],[],[],'db');


% figure;
% subplot(4, 1, 1); plot(20 * log10(abs(x))); title("original");
% subplot(4, 1, 2); plot(20 * log10(abs(xlow))); title("low pass");
% subplot(4, 1, 3); plot(20 * log10(abs(xhigh))); title("high pass");

% subplot(4, 1, 4); plot(20 * log10(abs(xlow)) - 20 * log10(abs(xhigh))); title("low - high");
% subplot(4, 1, 4); plot(20 * log10(abs(xlow - xhigh))); title("low - high");



y1 = hilbert(x);

% figure;
% subplot(2, 1, 1); plot(20 * log10(abs(x))); grid on; title("original");
% subplot(2, 1, 2); plot(20 * log10(abs(y1))); grid on; title("Hilbert");


% z = medfilt1(y1, 30);
% 
% figure;
% subplot(2, 1, 1); plot(20 * log10(abs(y1))); title("Hilbert");
% subplot(2, 1, 2); plot(20 * log10(abs(z))); title("Hilbert with median filter");


a    = fir1(80, 0.02);
xlow = filter(a, 1, x);
xlow = xlow(40:end);

y2 = hilbert(xlow);

% figure;
% subplot(2, 1, 1); plot(20 * log10(abs(xlow))); grid on; title("low-pass filter");
% subplot(2, 1, 2); plot(20 * log10(abs(y2))); grid on; title("Hilbert with low-pass filter");


figure;
subplot(3, 1, 1); plot(20 * log10(abs(y1))); grid on; title("Hilbert");
subplot(3, 1, 2); plot(20 * log10(abs(y2))); grid on; title("Hilbert with low-pass filter");

% z1 = filter(ones(40, 1), 1, y2);
% subplot(3, 1, 3); plot(20 * log10(abs(z1))); grid on; title("Subtraction");






