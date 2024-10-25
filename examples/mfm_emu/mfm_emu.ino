#include "drive.pio.h"
#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#define DEBUG_ASSERT(x) do { if (!(x)) { Serial.printf(__FILE__ ":%d: Assert fail: " #x "\n", __LINE__); } } while(0)
#include "mfm_impl.h"

volatile int trackno;
volatile int cached_trackno = -1;

#define DIRECTION_PIN (D8)
#define STEP_PIN (D9)
#define TRK0_PIN (D10)
#define INDEX_PIN (D11)
#define FLUX_PIN (D13)
#define STEP_IN (HIGH)

enum { flux_max_bits = 200000 }; // 300RPM (200ms rotational time), 1us bit times
// enum { flux_max_bits = 10000 }; // 300RPM (200ms rotational time), 1us bit times

enum { sector_count=18, track_max_bytes = sector_count * mfm_io_block_size };

uint8_t track_data[track_max_bytes];
uint32_t flux_data[(flux_max_bits + 31) / 32];

volatile bool updated = false;
volatile bool driveConnected = false;
volatile bool appUsing = false;

#if 0
// Called by FatFSUSB when the drive is released.  We note this, restart FatFS, and tell the main loop to rescan.
void unplug(uint32_t i) {
  (void) i;
  driveConnected = false;
  updated = true;
  FatFS.begin();
}

// Called by FatFSUSB when the drive is mounted by the PC.  Have to stop FatFS, since the drive data can change, note it, and continue.
void plug(uint32_t i) {
  (void) i;
  driveConnected = true;
  FatFS.end();
}

// Called by FatFSUSB to determine if it is safe to let the PC mount the USB drive.  If we're accessing the FS in any way, have any Files open, etc. then it's not safe to let the PC mount the drive.
bool mountable(uint32_t i) {
  (void) i;
  return !appUsing;
}
#endif

void stepped() {
    auto direction = digitalRead(DIRECTION_PIN);
    int new_track = trackno;
    if(direction == STEP_IN) {
        if (new_track > 0) new_track--;
    } else {
        if (new_track < 79) new_track++;
    }
    trackno = new_track;
    digitalWrite(TRK0_PIN, trackno != 0); // active LOW
}

PIO pio = pio0;
uint sm_fluxout, offset_fluxout;
uint sm_index_pulse, offset_index_pulse;

void setupPio() {
   offset_fluxout = pio_add_program(pio, &fluxout_compact_program);
   sm_fluxout = pio_claim_unused_sm(pio, true);
   fluxout_compact_program_init(pio, sm_fluxout, offset_fluxout, FLUX_PIN, 1000);

   offset_index_pulse = pio_add_program(pio, &index_pulse_program);
   sm_index_pulse = pio_claim_unused_sm(pio, true);
   index_pulse_program_init(pio, sm_index_pulse, offset_index_pulse, INDEX_PIN, 1000);

 
    Serial.printf("fluxout sm%d offset%d\n", sm_fluxout, offset_fluxout);
    Serial.printf("index_pulse sm%d offset%d\n", sm_index_pulse, offset_index_pulse);
}

void setup1() {
  while (!Serial) {                                                                          
    delay(1);                                                                                
  }                                                                                          
    Serial.println("(in setup1, naughty)");                                          
  setupPio();

}

void setup() {
  pinMode(DIRECTION_PIN, INPUT_PULLUP);
  pinMode(STEP_PIN, INPUT_PULLUP);
  pinMode(TRK0_PIN, OUTPUT);
  // pinMode(INDEX_PIN, OUTPUT);

  for(auto &d : flux_data) d = 0xaa55cc11;
  
  Serial.begin(115200);                                                                      
  while (!Serial) {                                                                          
    delay(1);                                                                                
  }                                                                                          
  // delay(5000);                                                                               
  attachInterrupt(digitalPinToInterrupt(STEP_PIN), stepped, FALLING);

#if 0
                                                                                             
  if (!FatFS.begin()) {                                                                      
    Serial.println("FatFS initialization failed!");                                          
    while (1) {                                                                              
      delay(1);                                                                              
    }                                                                                        
  }
  Serial.println("FatFS initialization done.");

  FatFSUSB.onUnplug(unplug);
  FatFSUSB.onPlug(plug);
  FatFSUSB.driveReady(mountable);

  Serial.println("FatFSUSB started.");
  Serial.println("Connect drive via USB to upload/erase files and re-display");
#endif

}

volatile int revs;

#define flux_valid() (cached_trackno == trackno)
void __not_in_flash_func(loop1)() {
    static bool once;
    if(flux_valid()) {
        pio_sm_put_blocking(pio, sm_index_pulse, 4000); // ??? put index high for 4ms (out of 200ms)
        revs++;
        for(auto d : flux_data) {
            if (!flux_valid()) break;
            pio_sm_put_blocking(pio, sm_fluxout, __builtin_bswap32(d));
        }
    }
}

static void encode_track_mfm(uint8_t head, uint8_t cylinder, uint8_t n_sectors) {

    mfm_io_t io = {
        .encode_compact = true,
        .T2_max = 5,
        .T3_max = 7,
        .T1_nom = 2,
        .pulses = reinterpret_cast<uint8_t*>(flux_data),
        .n_pulses = sizeof(flux_data),
        //.pos = 0,
        .sectors = track_data,
        .n_sectors = n_sectors,
        //.sector_validity = NULL,
        .head = head,
        .cylinder = cylinder,
    };

    encode_track_mfm(&io);
Serial.printf("encoded to %zu bits\n", io.pos * CHAR_BIT);
}
 
int old_revs =-1;
void loop() {
    {
        auto tmp = trackno;
        if(tmp != cached_trackno) {
            Serial.printf("Stepped to track %d\n", tmp);
            std::fill(track_data, std::end(track_data), trackno);
            encode_track_mfm(0, tmp, sector_count);
            cached_trackno = tmp;
        }
    }

    {
        auto tmp = revs; 
        if(tmp != old_revs) {
            printf("%d revs\n", tmp);
            old_revs = tmp;
        }   
    }
// Serial.printf("%2d\r", (int)digitalRead(STEP_PIN));
}
