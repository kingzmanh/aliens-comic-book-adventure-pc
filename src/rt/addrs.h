/* Game addresses the runtime uses, per executable build (relocated).
 * English: ALIENSP2.EXE of the official 1.03US update.
 * French:  ALIENSP2.EXE 1.02F on the French CD 1 (build with -DGAME_FR=1).
 * The French values come from tools/xmap.py (the same functions found by
 * their bytes, data addresses paired through the LE fixups). */
#ifndef ADDRS_H
#define ADDRS_H
#ifndef GAME_FR
#define GAME_FR 0
#endif

#if GAME_FR
#define GAME_LANG       "fr"
#define A_CODE_END      0x4826e3u
#define A_ENTRY         0x463a2cu
/* Watcom startup globals */
#define W_STACKTOP      0x4e1308u
#define W_CURBRK        0x4e130cu
#define W_DYNEND        0x4e1310u
#define W_PGMNAME       0x4e1314u
#define W_PSP           0x4e1318u
#define W_STACKLOW      0x4e131cu
#define W_STKTOP0       0x4e1320u
#define W_NO87          0x4e1338u
#define W_EXTENDER      0x4e133au
#define W_EXTSUB        0x4e133bu
#define W_ENVPTR        0x4e133du
#define W_ENVSEL        0x4e1341u
#define W_OSMAJOR       0x4e1343u
#define W_OSMINOR       0x4e1344u
#define W_INITRTNS      0x475636u
#define W_CMAIN         0x4755e6u
#define A_ARGBUF        0x6341a0u
/* keyboard typematic, graph lib */
#define A_KBD_RATE      0x4df159u
#define A_KBD_DELAY     0x4df15au
#define A_GR_PLOTACT    0x4dff5bu
/* sound system / HMI SOS */
#define A_SND_VOICES    0x4d9ba4u
#define A_SND_FLAGS     0x4d9bacu
#define A_SND_SLOTS     0x4d9c0cu
#define A_SND_NVOICE2   0x4db924u
#define A_SND_UP        0x4dedb0u
#define A_SOS_HANDLES   0x61b340u
#define A_SOS_SAMPLES   0x61b440u
#define A_SOS_DRIVER    0x61b614u
/* MIDI module */
#define A_MIDI_DEVICE   0x4db928u
#define A_MIDI_UP       0x4db9e4u
#define A_MIDI_VOLUME   0x4db9e8u
#define A_MIDI_HANDLE   0x4db9ecu
#define A_MIDI_LOADED   0x4db9f0u
#define A_MIDI_NAME     0x61b770u
#else
#define GAME_LANG       "en"
#define A_CODE_END      0x481f53u
#define A_ENTRY         0x46331cu
#define W_STACKTOP      0x4df64cu
#define W_CURBRK        0x4df650u
#define W_DYNEND        0x4df654u
#define W_PGMNAME       0x4df658u
#define W_PSP           0x4df65cu
#define W_STACKLOW      0x4df660u
#define W_STKTOP0       0x4df664u
#define W_NO87          0x4df67cu
#define W_EXTENDER      0x4df67eu
#define W_EXTSUB        0x4df67fu
#define W_ENVPTR        0x4df681u
#define W_ENVSEL        0x4df685u
#define W_OSMAJOR       0x4df687u
#define W_OSMINOR       0x4df688u
#define W_INITRTNS      0x474ec6u
#define W_CMAIN         0x474e76u
#define A_ARGBUF        0x6324e0u
#define A_KBD_RATE      0x4dd499u
#define A_KBD_DELAY     0x4dd49au
#define A_GR_PLOTACT    0x4de29fu
#define A_SND_VOICES    0x4d7ee4u
#define A_SND_FLAGS     0x4d7eecu
#define A_SND_SLOTS     0x4d7f4cu
#define A_SND_NVOICE2   0x4d9c64u
#define A_SND_UP        0x4dd0f0u
#define A_SOS_HANDLES   0x619680u
#define A_SOS_SAMPLES   0x619780u
#define A_SOS_DRIVER    0x619954u
#define A_MIDI_DEVICE   0x4d9c68u
#define A_MIDI_UP       0x4d9d24u
#define A_MIDI_VOLUME   0x4d9d28u
#define A_MIDI_HANDLE   0x4d9d2cu
#define A_MIDI_LOADED   0x4d9d30u
#define A_MIDI_NAME     0x619ab0u
#endif

#endif
