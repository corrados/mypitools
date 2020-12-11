function drumtrigger

% Drum trigger tests

% using a recording of a Roland snare mesh pad

close all;
pkg load signal

org = audioread("snare.wav");
org = org(:, 1); % just the left channel contains all the data

% figure; plot(20 * log10(abs(x)));

x_pos_sense = resample(org(730000:980000), 1, 6);
x_roll      = resample(org(395000:510000), 1, 6);


[pos_sense_metric, peak_start_with_mask, hil] = calc_pos_sense_metric(x_pos_sense, -50, 600);
figure; plot(20 * log10(abs(hil))); hold on; plot(peak_start_with_mask, pos_sense_metric - 40, 'r*'); title("pos sense signal");
% figure; subplot(2, 1, 1); plot(20 * log10(abs(hil))); grid on; title("pos sense signal"); ax = axis;
% subplot(2, 1, 2); plot(peak_start_with_mask, pos_sense_metric, 'r*'); grid on; title("Positional sensing metric"); axis([ax(1), ax(2)]);

[pos_sense_metric, peak_start_with_mask, hil] = calc_pos_sense_metric(x_roll, -20, 100);
figure; plot(20 * log10(abs(hil))); hold on; plot(peak_start_with_mask, pos_sense_metric - 30, 'r*'); title("roll signal");
% figure; subplot(2, 1, 1); plot(20 * log10(abs(hil))); grid on; title("roll signal"); ax = axis;
% subplot(2, 1, 2); plot(peak_start_with_mask, pos_sense_metric, 'r*'); grid on; title("Positional sensing metric"); axis([ax(1), ax(2)]);

end

function [pos_sense_metric, peak_start_with_mask, hil] = calc_pos_sense_metric(x, threshold_db, mask_time_samples)

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

% threshold_db      = -50; % works good for pos_sense signal
% mask_time_samples = 600; % works good for pos_sense signal
energy_window_len = 16; % 2 ms scan time at fs = 8 kHz

hil = hilbert(x);

% figure;
% subplot(2, 1, 1); plot(20 * log10(abs(x))); grid on; title("original");
% subplot(2, 1, 2); plot(20 * log10(abs(hil))); grid on; title("Hilbert");


% first order IIR filter
alpha   = 0.1;
hil_iir = filter(alpha, [1, alpha - 1], hil);

% simple threshold
above_thresh         = hil_iir > 10 ^ (threshold_db / 20);
peak_start           = find(diff(above_thresh) > 0);
peak_start_with_mask = [];

if ~isempty(peak_start)

  peak_start_with_mask = peak_start(1);
  for i = 2:length(peak_start)

    if peak_start(i) > peak_start_with_mask(end) + mask_time_samples

      peak_start_with_mask(length(peak_start_with_mask) + 1) = peak_start(i);

    end

  end

end

% low pass filter of the Hilbert signal
a    = fir1(80, 0.02);
xlow = filter(a, 1, x);
xlow = xlow(40:end);

hil_low = hilbert(xlow);

% figure;
% subplot(2, 1, 1); plot(20 * log10(abs(xlow))); grid on; title("low-pass filter");
% subplot(2, 1, 2); plot(20 * log10(abs(hil_low))); grid on; title("Hilbert with low-pass filter");

peak_start_with_mask = peak_start_with_mask(peak_start_with_mask + energy_window_len - 1 <= length(hil));
peak_energy          = [];
peak_energy_low      = [];

for i = 1:length(peak_start_with_mask)

  peak_energy(i)     = sum(abs(hil(peak_start_with_mask(i):peak_start_with_mask(i) + energy_window_len - 1)) .^ 2);
  peak_energy_low(i) = sum(abs(hil_low(peak_start_with_mask(i):peak_start_with_mask(i) + energy_window_len - 1)) .^ 2);

end
% 10 * log10(peak_energy)
% 10 * log10(peak_energy_low)
pos_sense_metric = 10 * log10(peak_energy) - 10 * log10(peak_energy_low);

figure;
subplot(3, 1, 1); plot(20 * log10(abs([hil, hil_iir, above_thresh]))); grid on; title("Hilbert");
subplot(3, 1, 2); plot(20 * log10(abs(hil_low))); grid on; title("Hilbert with low-pass filter"); ax = axis;
subplot(3, 1, 3); plot(peak_start_with_mask, pos_sense_metric, 'r*'); grid on; title("Positional sensing metric"); axis([ax(1), ax(2)]);

end


