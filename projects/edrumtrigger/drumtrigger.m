function drumtrigger

% Drum trigger tests

close all;
pkg load signal
pkg load audio


% TEST for continuous audio data capturing and processing
% continuous_recording(1, 8000, @(x, do_realtime) processing(x, do_realtime));

% TEST process recordings
% x = audioread("pd120_pos_sense.wav");
x = audioread("pd120_pos_sense2.wav");
% x = audioread("pd120_single_hits.wav");
% x = audioread("pd120_roll.wav");
% x = audioread("pd120_middle_velocity.wav");
% x = audioread("pd120_hot_spot.wav");
% x = audioread("pd6.wav");
% org = audioread("snare.wav"); x = resample(org(:, 1), 1, 6); % PD-120


%x_edge   = x(26200:28000);
%x_middle = x(3000:4200);
%figure; subplot(2, 1, 1), pwelch(x_middle,[],[],[],[],'twosided','db'); title('middle');
%subplot(2, 1, 2), pwelch(x_edge,[],[],[],[],'twosided','db'); title('edge');
%figure; freqz(fir1(80, 0.02));

% hil = myhilbert(x);
% figure; plot(20 * log10(abs([x, hilbert(x)])));
% figure; plot(20 * log10(abs([x, myhilbert(x)]))); title('myhilbert');

processing(x, false);

end


function hil = myhilbert(x)

a   = fir1(6, 0.4);
a   = a .* exp(1j * 2 * pi * (0:length(a) - 1) * 0.3) * length(a);
hil = filter(a, 1, x);

% figure; freqz(a);
% figure;
% subplot(2, 1, 1), pwelch(x,[],[],[],[],'twosided','db');
% subplot(2, 1, 2), pwelch(hil,[],[],[],[],'twosided','db');

% TEST use built-in hilbert filter instead of my own implementation for reference
% hil = hilbert(x);

end


function [all_peaks, hil_filt_org] = calc_peak_detection(x)

hil = myhilbert(x);

threshold_db      = -60;%-45;
energy_window_len = 16; % 2 ms scan time at fs = 8 kHz
mask_time         = 65; % samples

% the following settings are trigger pad-specific (here, a PD-120 is used)
decay_len         = 1200;%1500; % samples
decay_att_db      = 1;%4;%7; % decay attenuation in dB
decay_grad        = 0.025;%0.05; % decay gradient factor

% alpha   = 0.1;
% hil_filt = filter(alpha, [1, alpha - 1], hil);

% moving average filter
hil_filt     = abs(filter(ones(energy_window_len, 1) / energy_window_len, 1, hil));
hil_filt_org = hil_filt;

last_peak_idx = 0;
all_peaks     = [];
i             = 1;
no_more_peak  = false;

% figure; plot(20 * log10([abs(x), hil_filt_org])); hold on;

while ~no_more_peak

  % find values above threshold, masking regions which are already done
  above_thresh = (hil_filt > 10 ^ (threshold_db / 20)) & [zeros(last_peak_idx, 1); ones(length(hil_filt) - last_peak_idx, 1)];
  peak_start   = find(diff(above_thresh) > 0);

  % exit condition
  if isempty(peak_start)
    no_more_peak = true;
    continue;
  end

  % climb to the maximum of the current peak
  peak_idx = peak_start(1);
  max_idx  = find(hil_filt(1 + peak_idx:end) - hil_filt(peak_idx:end - 1) < 0);

  % second exit condition
  if isempty(max_idx)
    no_more_peak = true;
    continue;
  end

  peak_idx      = peak_idx + max_idx(1) - 1;
  all_peaks     = [all_peaks; peak_idx];
  last_peak_idx = min(peak_idx + mask_time, length(hil_filt));

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

  % plot(decay_x, 20 * log10(decay), 'k');

end

% figure; plot(20 * log10([abs(x), hil_filt_org, hil_filt])); hold on;
% plot(all_peaks, 20 * log10(hil_filt(all_peaks)), 'k*');
% plot(decay_x, 20 * log10(decay), 'k');
% plot(decay_x, 20 * log10(hil_filt_new))

end


function pos_sense_metric = calc_pos_sense_metric(x, all_peaks)

energy_window_len = 16; % 2 ms scan time at fs = 8 kHz
lp_ir_len         = 80; % low-pass filter length
lp_cutoff         = 0.02; % normalized cut-off of low-pass filter

hil = myhilbert(x);

% low pass filter of the Hilbert signal
a       = fir1(lp_ir_len, lp_cutoff);
xlow    = filter(a, 1, x);
xlow    = xlow(lp_ir_len / 2:end);

% % TEST
% alpha = 0.01;
% xlow  = filter(alpha, [1, alpha - 1], x);

hil_low = myhilbert(xlow);

% figure; plot(20 * log10(abs([hil(1:length(hil_low)), hil_low]))); hold on;

peak_energy     = [];
peak_energy_low = [];

% figure; plot(20 * log10(abs(hil))); hold on;

for i = 1:length(all_peaks)

  win_idx            = (all_peaks(i):all_peaks(i) + energy_window_len - 1) - energy_window_len / 2;
  win_idx            = win_idx((win_idx <= length(hil_low)) & (win_idx > 0));
  peak_energy(i)     = sum(abs(hil(win_idx)) .^ 2);
  peak_energy_low(i) = sum(abs(hil_low(win_idx)) .^ 2);

  % plot(win_idx, 20 * log10(abs(hil(win_idx))), 'k.-');

end

pos_sense_metric = 10 * log10(peak_energy) - 10 * log10(peak_energy_low);

end


function processing(x, do_realtime)

% calculate peak detection and positional sensing
[all_peaks, hil_filt] = calc_peak_detection(x);
pos_sense_metric      = calc_pos_sense_metric(x, all_peaks);

if ~do_realtime
  figure % open figure to keep previous plots (not desired for real-time)
end

% plot results
cla
plot(20 * log10(abs([x, hil_filt]))); grid on; hold on;
plot(all_peaks, 20 * log10(hil_filt(all_peaks)), 'g*');
plot(all_peaks, pos_sense_metric - 40, 'k*');
title('red marker: level, black marker: position');
xlabel('samples'); ylabel('dB');
ylim([-100, 0]);
drawnow;


% TEST
% velocity/positional sensing mapping and play MIDI notes
velocity    = (20 * log10(hil_filt(all_peaks)) + 63) / 40 * 127;
velocity    = max(1, min(127, velocity));
pos_sensing = (pos_sense_metric - 20.5) / 15 * 127;
pos_sensing = max(1, min(127, pos_sensing));
% play_midi(all_peaks, velocity, pos_sensing);

end


function continuous_recording(blocklen, Fs, callbackfkt)

% continuous recording of audio data and processing in a callback function
recorder   = audiorecorder(Fs, 16, 1);
bDataReady = false;

while true

  while isrecording(recorder)
    pause(blocklen / 1000);
  end

  if bDataReady
    x = getaudiodata(recorder);
  end

  record(recorder, blocklen);

  if bDataReady
    callbackfkt(x, true);
  end

  bDataReady = true;

end

end


function play_midi(all_peaks, velocity, pos_sensing)

dev = mididevice("output", "Lexicon Mac USB 1"); % Lexicon Omega -> TDW-20
x   = now;
y   = x;

for i = 1:length(all_peaks)

  while y < x + all_peaks(i) / 8000 / 1e5
    pause(0.00001);
    y = now;
  end

  midisend(dev, midimsg("controlchange", 10, 16, pos_sensing(i))); % positional sensing
  midisend(dev, midimsg("note",          10, 38, velocity(i), 0.02));

end

end


