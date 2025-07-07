#include "icons.h"
#include "synth/arp.h"
#include "synth/params.h"
#include "synth/sequencer.h"

const static char* const pagenames[PG_LAST] = {

    [PG_SOUND1] = I_SLIDERS "Sound",
    [PG_SOUND2] = I_PIANO "Sound",
    [PG_ENV1] = I_ADSR_A "Env1",
    [PG_ENV2] = I_ADSR_A "Env2",
    [PG_ARP] = I_NOTES "Arp",
    [PG_SEQ] = I_NOTES "Seq",
    [PG_DELAY] = I_DELAY "Delay",
    [PG_REVERB] = I_REVERB "Reverb",
    [PG_A] = I_A,
    [PG_B] = I_B,
    [PG_X] = I_X,
    [PG_Y] = I_Y,
    [PG_SAMPLER] = I_WAVE "Sampler",
    [PG_JITTER] = I_RANDOM "Jitter"

};

const static char* const paramnames[P_LAST] = {
    [P_A2] = I_ADSR_A "Attack2",
    [P_D2] = I_ADSR_D "Decay2",
    [P_S2] = I_ADSR_S "Sustain2",
    [P_R2] = I_ADSR_R "Release2",
    [P_SWING] = I_TEMPO "Swing",

    [P_SENS] = I_TOUCH "Sensitivity",
    [P_DRIVE] = I_DISTORT "Distort",
    [P_A] = I_ADSR_A "Attack",
    [P_D] = I_ADSR_D "Decay",
    [P_S] = I_ADSR_S "Sustain",
    [P_R] = I_ADSR_R "Release",

    [P_MIXSYNTH] = I_WAVE "Synth Lvl",
    [P_MIXINPUT] = I_JACK "Input Lvl",
    [P_MIXINWETDRY] = I_JACK "In Wet/Dry",
    [P_MIXWETDRY] = I_REVERB "Main Wet/Dry",
    [P_MIXHPF] = I_HPF "High Pass",
    [P_MIXRESO] = I_DISTORT "Resonance",

    [P_OCT] = I_OCTAVE "Octave",
    [P_PITCH] = I_PIANO "Pitch",
    [P_GLIDE] = I_GLIDE "Glide",
    [P_INTERVAL] = I_INTERVAL "Interval",
    [P_GATE_LENGTH] = I_INTERVAL "Gate Len",
    [P_ENV_LEVEL] = I_AMPLITUDE "Env Level",

    [P_PWM] = "Shape",
    [P_RVUNUSED] = "<unused>",

    [P_SCALE] = I_PIANO "Scale",
    [P_ROTATE] = I_FEEDBACK "Degree",
    [P_MICROTUNE] = I_MICRO "Microtone",
    [P_STRIDE] = I_INTERVAL "Stride",

    [P_ARPONOFF] = I_NOTES "Arp On/Off",
    [P_LATCHONOFF] = "Latch On/Off",

    [P_ARPMODE] = I_ORDER "Arp",
    [P_ARPDIV] = I_DIVIDE "Divide",
    [P_ARPPROB] = I_PERCENT "Prob %",
    [P_ARPLEN] = I_LENGTH "Euclid Len",
    [P_ARPOCT] = I_OCTAVE "Octaves",
    [P_TEMPO] = "BPM",

    [P_SEQMODE] = I_ORDER "Seq",
    [P_SEQDIV] = I_DIVIDE "Divide",
    [P_SEQPROB] = I_PERCENT "Prob %",
    [P_SEQLEN] = I_LENGTH "Euclid Len",
    [P_SEQSTEP] = I_SEQ "Step Ofs",
    [P_SEQPAT] = I_PRESET "Pattern",

    [P_DLSEND] = I_SEND "Send",
    [P_DLTIME] = I_TIME "Time",
    [P_DLFB] = I_FEEDBACK "Feedback",
    //[P_DLCOLOR]=I_COLOR "Colour",
    [P_DLWOB] = I_AMPLITUDE "Wobble",
    [P_DLRATIO] = I_DIVIDE "2nd Tap",

    [P_RVSEND] = I_SEND "Send",
    [P_RVTIME] = I_TIME "Time",
    [P_RVSHIM] = I_FEEDBACK "Shimmer",
    //[P_RVCOLOR]=I_COLOR "Colour",
    [P_RVWOB] = I_AMPLITUDE "Wobble",
    //[P_RVUNUSED]=" ",

    [P_SAMPLE] = I_WAVE "Sample",
    [P_SMP_POS] = "Scrub",
    [P_SMP_RATE] = I_NOTES "Rate",
    [P_SMP_GRAINSIZE] = I_PERIOD "Grain Sz",
    [P_SMP_TIME] = I_TIME "Timestretch",
    [P_CV_QUANT] = I_JACK "CV Quantise",

    [P_ACCEL_SENS] = I_AMPLITUDE "Accel Sens",
    [P_MIX_WIDTH] = I_AMPLITUDE "Stereo Width",
    [P_NOISE] = I_WAVE "Noise",
    [P_JIT_POS] = "Scrub",
    [P_JIT_RATE] = I_NOTES "Rate",
    [P_JIT_GRAINSIZE] = I_PERIOD "Grain Sz",
    [P_JIT_PULSE] = I_ENV "<unused>",
    [P_HEADPHONE] = "'Phones Vol",

    [P_AOFFSET] = I_OFFSET "CV Offset",
    [P_ASCALE] = I_TIMES "CV Scale",
    [P_ADEPTH] = I_AMPLITUDE "LFO Depth",
    [P_AFREQ] = I_PERIOD "LFO Rate",
    [P_ASHAPE] = I_SHAPE "LFO Shape",
    [P_AWARP] = I_WARP "LFO Warp",

    [P_BOFFSET] = I_OFFSET "CV Offset",
    [P_BSCALE] = I_TIMES "CV Scale",
    [P_BDEPTH] = I_AMPLITUDE "LFO Depth",
    [P_BFREQ] = I_PERIOD "LFO Rate",
    [P_BSHAPE] = I_SHAPE "LFO Shape",
    [P_BWARP] = I_WARP "LFO Warp",

    [P_XOFFSET] = I_OFFSET "CV Offset",
    [P_XSCALE] = I_TIMES "CV Scale",
    [P_XDEPTH] = I_AMPLITUDE "LFO Depth",
    [P_XFREQ] = I_PERIOD "LFO Rate",
    [P_XSHAPE] = I_SHAPE "LFO Shape",
    [P_XWARP] = I_WARP "LFO Warp",

    [P_YOFFSET] = I_OFFSET "CV Offset",
    [P_YSCALE] = I_TIMES "CV Scale",
    [P_YDEPTH] = I_AMPLITUDE "LFO Depth",
    [P_YFREQ] = I_PERIOD "LFO Rate",
    [P_YSHAPE] = I_SHAPE "LFO Shape",
    [P_YWARP] = I_WARP "LFO Warp",

    [P_MIDI_CH_IN] = I_PIANO "MIDI In Ch",
    [P_MIDI_CH_OUT] = I_PIANO "MIDI Out Ch",

};

const static char* const modnames[M_LAST] = {
    [M_BASE] = I_SLIDERS "Base", [M_RND] = I_RANDOM "Random", [M_ENV] = I_ENV "Env", [M_PRESSURE] = I_TOUCH "Pressure",
    [M_A] = I_A "Knob/LFO",      [M_B] = I_B "Knob/LFO",      [M_X] = I_X "CV/LFO",  [M_Y] = I_Y "CV/LFO",
};

const static char* const arp_modenames[NUM_ARP_ORDERS] = {
    [ARP_UP] = "Up",
    [ARP_DOWN] = "Down",
    [ARP_UPDOWN] = "Up/Down",
    [ARP_UPDOWN_REP] = "Up/Down\nRepeat",
    [ARP_PEDAL_UP] = "Pedal    \nUp",
    [ARP_PEDAL_DOWN] = "Pedal    \nDown",
    [ARP_PEDAL_UPDOWN] = "Pedal\nUp/Down",
    [ARP_RANDOM] = "Rnd",
    [ARP_RANDOM2] = "2xRnd",
    [ARP_CHORD] = "Chord",
    [ARP_UP8] = "Up\n8 Steps",
    [ARP_DOWN8] = "Down\n8 Steps",
    [ARP_UPDOWN8] = "Up/Down\n8 Steps",
    [ARP_RANDOM8] = "Rnd\n8 Steps",
    [ARP_RANDOM28] = "2xRnd\n8 Steps",
};

const static char* const seqmodenames[NUM_SEQ_ORDERS] = {
    [SEQ_ORD_PAUSE] = "Pause",
    [SEQ_ORD_FWD] = "Forward",
    [SEQ_ORD_BACK] = "Reverse",
    [SEQ_ORD_PINGPONG] = "Pingpong",
    [SEQ_ORD_PINGPONG_REP] = "PingPong Rep",
    [SEQ_ORD_RANDOM] = "Random",
};