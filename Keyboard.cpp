#include "daisy_seed.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisy::seed;
using namespace daisysp;

static DaisySeed hw;

const int NUM_ROWS = 5;
const int NUM_COLS = 12;

const daisy::Pin ROW_PINS[NUM_ROWS] = {D0, D1, D2, D3, D4};

const daisy::Pin DATA_PIN = D5;
const daisy::Pin LATCH_PIN = D6;
const daisy::Pin CLOCK_PIN = D7;

bool keyPressed[NUM_ROWS][NUM_COLS];

//Note frequencies corresponding to each key
const float NOTES[NUM_ROWS][NUM_COLS] = {{523.25f, 554.37f, 587.33f, 622.25f, 659.25f, 698.46f, 739.99f, 783.99f, 830.61f, 880.00f, 932.33f, 987.77f}, 
                                        {261.63f, 277.18f, 293.66f, 311.13f, 329.63f, 349.23f, 369.99f, 392.00f, 415.30f, 440.00f, 466.16f, 493.88f},
                                        {130.81f, 138.59f, 146.83f, 155.56f, 164.81f, 174.61f, 185.00f, 196.00f, 207.65f, 220.00f, 233.08f, 246.94f},
                                        {65.41f, 69.30f, 73.42f, 77.78f, 82.41f, 87.31f, 92.50f, 98.00f, 103.83f, 110.00f, 116.54f, 123.47f},
                                        {1046.50f}};

//Bytes corresponding to each keyboard col connected to the shift registers
const uint8_t BITS[] = {0b00000001, 0b00000010, 0b00000100, 0b00001000, 0b00010000, 0b00100000, 0b01000000, 0b10000000};
//Bitmask for checking the greates digit of the bytes
const uint8_t BITMASK = 0b10000000;

static Oscillator osc;
static Adsr env;
bool gate;
float lastNote;
int wave = 0;
ReverbSc verb;
Overdrive drive;
Phaser phaser;
Tremolo    trem;

void AudioCallback(AudioHandle::InterleavingInputBuffer in, AudioHandle::InterleavingOutputBuffer out, size_t size)
{
    //Where the audio gets processed
    float sig, env_out;
	for (size_t i = 0; i < size; i++)
	{
        //Play the note currently pressed
        osc.SetFreq(lastNote);
        //Use the waveform selected on the second potentiometer
        osc.SetWaveform(wave);

        //Start the envelope
        env_out = env.Process(gate);
        //Use the volume selected on the first potentiometer
        osc.SetAmp(env_out * (1 - hw.adc.GetFloat(0)));

        //Use the overdrive value set on the fifth potentiometer
        drive.SetDrive(1 - hw.adc.GetFloat(4));
        if(1 - hw.adc.GetFloat(4) < 0.009f){
            //Minimum drive value to still hear note
            drive.SetDrive(0.009f);
        }

        //Use the phaser value set on the sixth potentiometer
        phaser.SetLfoDepth(1 - hw.adc.GetFloat(5));

        //Use the tremolo values set on the seventh and eighth potentiometers
        trem.SetFreq(1 - hw.adc.GetFloat(6));
        trem.SetDepth(1 - hw.adc.GetFloat(7));

        //Process all the effects
        sig = drive.Process(phaser.Process(trem.Process(osc.Process())));

        //Use the reverb values set on the third and fourth potentiometers
        verb.SetFeedback(1 - hw.adc.GetFloat(2));
        verb.SetLpFreq((1 - hw.adc.GetFloat(3)) * 10000 + 100);

        if((1 - hw.adc.GetFloat(3)) * 10000 < 100 || 1 - hw.adc.GetFloat(2) < 0.1f){
            //If the reverb settings are negligible, do not process the reverb and output the signal
            out[i] = sig;
		    out[i + 1] = sig;
        }else{
            //If the reverb settings are not negligible, process the reverb and output the signal
            verb.Process(sig, sig, &out[i], &out[i + 1]);
        }
	}
}

int main(void)
{
    //Initialize the Daisy Seed
    hw.Init();
    hw.SetAudioBlockSize(4);
    float sample_rate;
    sample_rate = hw.AudioSampleRate();
    
    //Initialize the envelope. The envelope changes how the note sounds over time
    env.Init(sample_rate);
    
    //Initialize the oscillator
    osc.Init(sample_rate);
    osc.SetWaveform(wave);
    osc.SetFreq(300);
    
    //Initialize the overdrive
    drive.Init();

    //Initialize the phaser
    phaser.Init(sample_rate);
    phaser.SetFreq(500.f);
    phaser.SetLfoDepth(0.f);

    //Initialize the tremolo
    trem.Init(sample_rate);

    //Initialize the reverb
    verb.Init(sample_rate);
    verb.SetFeedback(0.9f);
    verb.SetLpFreq(18000.0f);

    //Set the envelope values
    env.SetTime(ADSR_SEG_ATTACK, .1);
    env.SetTime(ADSR_SEG_DECAY, .1);
    env.SetTime(ADSR_SEG_RELEASE, .08);

    env.SetSustainLevel(.25);

    //Potentiometer setup
    AdcChannelConfig adc[8];
    //Volume
    adc[0].InitSingle(hw.GetPin(15));
    //Waveform
    adc[1].InitSingle(hw.GetPin(16));
    //Reverb feedback
    adc[2].InitSingle(hw.GetPin(17));
    //Reverb frequency
    adc[3].InitSingle(hw.GetPin(18));
    //Drive
    adc[4].InitSingle(hw.GetPin(19));
    //Phaser
    adc[5].InitSingle(hw.GetPin(20));
    //Tremolo frequency
    adc[6].InitSingle(hw.GetPin(21));
    //Tremelo depth
    adc[7].InitSingle(hw.GetPin(22));
    
    hw.adc.Init(adc, 8);
    
    hw.adc.Start();

    //Shift Register setup. The shift registers will handle the 12 columns
    GPIO data;
    GPIO clock;
    GPIO latch;
    
    data.Init(DATA_PIN, GPIO::Mode::OUTPUT, GPIO::Pull::NOPULL);
    data.Write(true);
    clock.Init(CLOCK_PIN, GPIO::Mode::OUTPUT, GPIO::Pull::NOPULL);
    clock.Write(true);
    latch.Init(LATCH_PIN, GPIO::Mode::OUTPUT, GPIO::Pull::NOPULL);
    latch.Write(true);

    //Row pin setup
    GPIO row1Pin;
    GPIO row2Pin;
    GPIO row3Pin;
    GPIO row4Pin;
    GPIO row5Pin;

    GPIO rowPinsGPIO[] = {row1Pin, row2Pin, row3Pin, row4Pin, row5Pin};

    for(int k = 0; k < NUM_ROWS; k++){
        rowPinsGPIO[k].Init(ROW_PINS[k], GPIO::Mode::INPUT, GPIO::Pull::PULLDOWN, GPIO::Speed::LOW);
        rowPinsGPIO[k].Write(true);
    }
    //Start audio
    hw.StartAudio(AudioCallback);

    while(1)
    {
        //Get waveform from second potentiometer
        if(1 - hw.adc.GetFloat(1) < .2f){
            //Sine wave
            wave = 0;
        }else if (1 - hw.adc.GetFloat(1) < .4f)
        {
            //Triangle wave
            wave = 1;
        }else if (1 - hw.adc.GetFloat(1) < .6f)
        {
            //Sawtooth wave
            wave = 2;
        }else if (1 - hw.adc.GetFloat(1) < .8f)
        {
            //Ramp or reverse sawtooth wave
            wave = 3;
        }else{
            //Square wave
            wave = 4;
        }

        //Scanning process for keys
        for(int i = 0; i < NUM_COLS; i++){
            latch.Write(false);

            if(0 <= i && i <= 7){ 
                //If col on the first shift register

                //shift out 00000000
                for(int bit  = 0; bit < 8; bit++){
                    data.Write(false);

                    clock.Write(false);
                    System::DelayUs(1);
                    clock.Write(true);
                    System::DelayUs(1);
                }
                
                uint8_t colByte = BITS[i];

                //shift out data
                for(int bit = 0; bit < 8; bit++){
                    if((colByte & BITMASK) == BITMASK){
                        //Check if the greatest bit is a 1 and pull the shift register's data pin high if so
                        data.Write(true);
                    }else{
                        //If the greatest bit is a 0, then pull the shift register's data pin low
                        data.Write(false);
                    }
                    //Move each bit in the byte down one position
                    colByte <<= 1;

                    //Output the 0 or 1 from the data pin
                    clock.Write(false);
                    System::DelayUs(1);
                    clock.Write(true);
                    System::DelayUs(1);
                }
            }else{
                //If col on the second shift register
                uint8_t colByte = BITS[i-8];
                
				//shift out data
                for(int bit = 0; bit < 8; bit++){
                    if((colByte & BITMASK) == BITMASK){
                        //Check if the greatest bit is a 1 and pull the shift register's data pin high if so
                        data.Write(true);
                    }else{
                        //If the greatest bit is a 0, then pull the shift register's data pin low
                        data.Write(false);
                    }
                    //Move each bit in the byte down one position
                    colByte <<= 1;

                    //Output the 0 or 1 from the data pin
                    clock.Write(false);
                    System::DelayUs(1);
                    clock.Write(true);
                    System::DelayUs(1);
                }
                //shift out 00000000
                for(int bit  = 0; bit < 8; bit++){
                    data.Write(false);

                    clock.Write(false);
                    System::DelayUs(1);
                    clock.Write(true);
                    System::DelayUs(1);
                }
                
            }
            //Output the data
            latch.Write(true);
            System::DelayUs(1);
            latch.Write(false);
            
            //get row values at this col
            for(int j = 0; j < NUM_ROWS; j++){
                if(rowPinsGPIO[j].Read() != false && !keyPressed[j][i]){ 
                    //If a key is pressed and was not already pressed, then set it to pressed and play the note corresponding with the key
                    keyPressed[j][i] = true;
                    lastNote = NOTES[j][i];
                    //Gate that will trigger the envelope to start
                    gate = true;
                }
            }
            
            for(int j = 0; j < NUM_ROWS; j++){
                if(rowPinsGPIO[j].Read() == false && keyPressed[j][i]){
                    //If a key is not pressed and was pressed before, then set it to not pressed
                    keyPressed[j][i] = false;
                    if(NOTES[j][i] == lastNote){
                        //If the key that was released corresponded to the note that was playing, then trigger the envelope to stop
                        gate = false;
                    }
                }
            }
        }
    }
}
