function drumtrigger

% Drum trigger tests

% using a recording of a Roland snare mesh pad


% TEST for continuous audio data capturing and processing
% ContinuousRecording(0.5, 4000, @(x) Callback(x));


close all;
pkg load signal

org = audioread("snare.wav");
org = org(:, 1); % just the left channel contains all the data

% figure; plot(20 * log10(abs(org)));

x_sgl_hits  = resample(org(40000:100000), 1, 6);
x_pos_sense = resample(org(730000:980000), 1, 6);
x_roll      = resample(org(395000:510000), 1, 6);
% x_roll      = resample(org(395000:410000), 1, 6);


x                     = resample(org, 1, 6);
[all_peaks, hil_filt] = calc_peak_detection(x);
pos_sense_metric      = calc_pos_sense_metric(x, all_peaks);

figure;
plot(20 * log10(abs(x))); grid on; hold on;
plot(all_peaks, 20 * log10(hil_filt(all_peaks)), 'k*');
plot(all_peaks, pos_sense_metric - 40, 'r*');
title('black marker: level, red marker: position');
xlabel('samples'); ylabel('dB');
ylim([-100, 0]);


% using old calc_pos_sense_metric function:
% [pos_sense_metric, peak_start_with_mask, hil] = calc_pos_sense_metric(x_pos_sense, -50, 600);
% figure; plot(20 * log10(abs(hil))); hold on; plot(peak_start_with_mask, pos_sense_metric - 40, 'r*'); title("pos sense signal");
% % figure; subplot(2, 1, 1); plot(20 * log10(abs(hil))); grid on; title("pos sense signal"); ax = axis;
% % subplot(2, 1, 2); plot(peak_start_with_mask, pos_sense_metric, 'r*'); grid on; title("Positional sensing metric"); axis([ax(1), ax(2)]);

% [pos_sense_metric, peak_start_with_mask, hil] = calc_pos_sense_metric(x_roll, -20, 100);
% figure; plot(20 * log10(abs(hil))); hold on; plot(peak_start_with_mask, pos_sense_metric - 30, 'r*'); title("roll signal");
% % figure; subplot(2, 1, 1); plot(20 * log10(abs(hil))); grid on; title("roll signal"); ax = axis;
% % subplot(2, 1, 2); plot(peak_start_with_mask, pos_sense_metric, 'r*'); grid on; title("Positional sensing metric"); axis([ax(1), ax(2)]);

end

function [all_peaks, hil_filt_org] = calc_peak_detection(x)

hil = hilbert(x);

energy_window_len = 16; % 2 ms scan time at fs = 8 kHz
decay_len         = 1500; % samples
mask_time         = 40; % samples
decay_att_db      = 10; % decay attenuation in dB
decay_grad        = 0.05; % decay gradient factor

% alpha   = 0.1;
% hil_filt = filter(alpha, [1, alpha - 1], hil);

% moving average filter
hil_filt     = abs(filter(ones(energy_window_len, 1) / energy_window_len, 1, hil));
hil_filt_org = hil_filt;

last_peak_idx = 0;
all_peaks     = [];
i             = 1;
no_more_peak  = false;

while ~no_more_peak

  % find values above threshold, masking regions which are already done
  above_thresh = (hil_filt > 10 ^ (-40 / 20)) & [zeros(last_peak_idx, 1); ones(length(hil_filt) - last_peak_idx, 1)];
  peak_start   = find(diff(above_thresh) > 0);

  % exit condition
  if isempty(peak_start)
    no_more_peak = true;
    continue;
  end

  % climb to the maximum of the current peak
  peak_idx      = peak_start(1);
  max_idx       = find(hil_filt(1 + peak_idx:end) - hil_filt(peak_idx:end - 1) < 0);
  peak_idx      = peak_idx + max_idx(1) - 1;
  all_peaks     = [all_peaks; peak_idx];
  last_peak_idx = peak_idx + mask_time;

  % exponential decay assumption
  decay           = hil_filt(peak_idx) * 10 ^ (-decay_att_db / 20) * 10 .^ (-(0:decay_len - 1) / 20 * decay_grad);
  decay_x         = peak_idx + (0:decay_len - 1);
  valid_decay_idx = decay_x <= length(hil_filt);
  decay           = decay(valid_decay_idx);
  decay_x         = decay_x(valid_decay_idx);

  % subtract decay (with clipping at zero)
  hil_filt_new                   = hil_filt(decay_x) - decay.';
  hil_filt_new(hil_filt_new < 0) = 0;

  % update filtered signal
  hil_filt(decay_x) = hil_filt_new;
  i                 = i + 1;

end

% figure; plot(20 * log10([hil_filt_org, hil_filt])); hold on;
% plot(all_peaks, 20 * log10(hil_filt(all_peaks)), 'k*');
% plot(decay_x, 20 * log10(decay), 'k');
% plot(decay_x, 20 * log10(hil_filt_new))

end


function pos_sense_metric = calc_pos_sense_metric(x, all_peaks)

energy_window_len = 16; % 2 ms scan time at fs = 8 kHz
lp_ir_len         = 80; % low-pass filter length
lp_cutoff         = 0.02; % normalized cut-off of low-pass filter

hil = hilbert(x);

% low pass filter of the Hilbert signal
a       = fir1(lp_ir_len, lp_cutoff);
xlow    = filter(a, 1, x);
xlow    = xlow(lp_ir_len / 2:end);
hil_low = hilbert(xlow);

peak_energy     = [];
peak_energy_low = [];

for i = 1:length(all_peaks)

  peak_energy(i)     = sum(abs(hil(all_peaks(i):all_peaks(i) + energy_window_len - 1)) .^ 2);
  peak_energy_low(i) = sum(abs(hil_low(all_peaks(i):all_peaks(i) + energy_window_len - 1)) .^ 2);

end

pos_sense_metric = 10 * log10(peak_energy) - 10 * log10(peak_energy_low);

end


% function [pos_sense_metric, peak_start_with_mask, hil] = calc_pos_sense_metric(x, threshold_db, mask_time_samples)
% 
% % threshold_db      = -50; % works good for pos_sense signal
% % mask_time_samples = 600; % works good for pos_sense signal
% energy_window_len = 16; % 2 ms scan time at fs = 8 kHz
% 
% hil = hilbert(x);
% 
% % figure;
% % subplot(2, 1, 1); plot(20 * log10(abs(x))); grid on; title("original");
% % subplot(2, 1, 2); plot(20 * log10(abs(hil))); grid on; title("Hilbert");
% 
% % first order IIR filter
% alpha   = 0.1;
% hil_iir = filter(alpha, [1, alpha - 1], hil);
% 
% % simple threshold
% above_thresh         = hil_iir > 10 ^ (threshold_db / 20);
% peak_start           = find(diff(above_thresh) > 0);
% peak_start_with_mask = [];
% 
% if ~isempty(peak_start)
% 
%   peak_start_with_mask = peak_start(1);
%   for i = 2:length(peak_start)
% 
%     if peak_start(i) > peak_start_with_mask(end) + mask_time_samples
% 
%       peak_start_with_mask(length(peak_start_with_mask) + 1) = peak_start(i);
% 
%     end
% 
%   end
% 
% end
% 
% % low pass filter of the Hilbert signal
% a    = fir1(80, 0.02);
% xlow = filter(a, 1, x);
% xlow = xlow(40:end);
% 
% hil_low = hilbert(xlow);
% 
% % figure;
% % subplot(2, 1, 1); plot(20 * log10(abs(xlow))); grid on; title("low-pass filter");
% % subplot(2, 1, 2); plot(20 * log10(abs(hil_low))); grid on; title("Hilbert with low-pass filter");
% 
% peak_start_with_mask = peak_start_with_mask(peak_start_with_mask + energy_window_len - 1 <= length(hil));
% peak_energy          = [];
% peak_energy_low      = [];
% 
% for i = 1:length(peak_start_with_mask)
% 
%   peak_energy(i)     = sum(abs(hil(peak_start_with_mask(i):peak_start_with_mask(i) + energy_window_len - 1)) .^ 2);
%   peak_energy_low(i) = sum(abs(hil_low(peak_start_with_mask(i):peak_start_with_mask(i) + energy_window_len - 1)) .^ 2);
% 
% end
% % 10 * log10(peak_energy)
% % 10 * log10(peak_energy_low)
% pos_sense_metric = 10 * log10(peak_energy) - 10 * log10(peak_energy_low);
% 
% figure;
% subplot(3, 1, 1); plot(20 * log10(abs([hil, hil_iir, above_thresh]))); grid on; title("Hilbert");
% subplot(3, 1, 2); plot(20 * log10(abs(hil_low))); grid on; title("Hilbert with low-pass filter"); ax = axis;
% subplot(3, 1, 3); plot(peak_start_with_mask, pos_sense_metric, 'r*'); grid on; title("Positional sensing metric"); axis([ax(1), ax(2)]);
% 
% end


function Callback(x)

  plot(x);
  drawnow;

end


function ContinuousRecording(blocklen, Fs, callbackfkt)

% continuous recording of audio data and processing in a callback function
recorder   = audiorecorder(Fs, 16, 1);
bDataReady = false;

while true

  while isrecording(recorder)
    pause(blocklen / 10);
  end

  if bDataReady
    x = getaudiodata(recorder);
  end

  record(recorder, blocklen);

  if bDataReady
    callbackfkt(x);
  end

  bDataReady = true;

end

end


