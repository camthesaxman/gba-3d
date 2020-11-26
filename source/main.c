#define NDEBUG

#include <assert.h>
#include <gba_console.h>
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <gba_sprites.h>
#include <gba_timers.h>
#include <gba_video.h>
#include <gba_input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "io_reg.h"
#include "macro.h"

#include "colormap.h"

#include "terrain_bin.h"

#include "r6502_portfont_bin.h"

struct OamData
{
    /*0x00*/ u32 y:8;
    /*0x01*/ u32 affineMode:2;  // 0x1, 0x2 = 0x3
             u32 objMode:2;     // 0x4, 0x8 = 0xC
             u32 mosaic:1;      // 0x10
             u32 bpp:1;         // 0x20
             u32 shape:2;       // 0x40, 0x80

    /*0x02*/ u32 x:9;
             u32 matrixNum:5; // bits 3/4 are h-flip/v-flip if not in affine mode
             u32 size:2;

    /*0x04*/ u16 tileNum:10;
             u16 priority:2;
             u16 paletteNum:4;
    /*0x06*/ u16 affineParam;
};

// represents a signed Q16.16 fixed point number
typedef s32 fixed_t;

static struct
{
    u16 keysDown;
    u16 prevKeys;
    u16 newKeys;
} input = {0};

struct
{
    /*0x00*/ fixed_t x;
    /*0x04*/ fixed_t y;
    /*0x08*/ s32 height;
    /*0x0C*/ s32 horizon;
    /*0x10*/ fixed_t sinYaw;
    /*0x14*/ fixed_t cosYaw;
    /*0x18*/ s16 yaw;
} camera;

// buffer to write to (this is the back buffer
u16 *frameBuffer;
static int fbNum = 0;

// Converts a number to Q8.8 fixed-point format
#define Q_8_8(n) ((s16)((n) * 256))

// Values of sin(x*(π/128)) as Q8.8 fixed-point numbers from x = 0 to x = 319
static const s16 gSineTable[] =
{
    Q_8_8(0),           // sin(0*(π/128))
    Q_8_8(0.0234375),   // sin(1*(π/128))
    Q_8_8(0.046875),    // sin(2*(π/128))
    Q_8_8(0.0703125),   // sin(3*(π/128))
    Q_8_8(0.09765625),  // sin(4*(π/128))
    Q_8_8(0.12109375),  // sin(5*(π/128))
    Q_8_8(0.14453125),  // sin(6*(π/128))
    Q_8_8(0.16796875),  // sin(7*(π/128))
    Q_8_8(0.19140625),  // sin(8*(π/128))
    Q_8_8(0.21875),     // sin(9*(π/128))
    Q_8_8(0.2421875),   // sin(10*(π/128))
    Q_8_8(0.265625),    // sin(11*(π/128))
    Q_8_8(0.2890625),   // sin(12*(π/128))
    Q_8_8(0.3125),      // sin(13*(π/128))
    Q_8_8(0.3359375),   // sin(14*(π/128))
    Q_8_8(0.359375),    // sin(15*(π/128))
    Q_8_8(0.37890625),  // sin(16*(π/128))
    Q_8_8(0.40234375),  // sin(17*(π/128))
    Q_8_8(0.42578125),  // sin(18*(π/128))
    Q_8_8(0.44921875),  // sin(19*(π/128))
    Q_8_8(0.46875),     // sin(20*(π/128))
    Q_8_8(0.4921875),   // sin(21*(π/128))
    Q_8_8(0.51171875),  // sin(22*(π/128))
    Q_8_8(0.53125),     // sin(23*(π/128))
    Q_8_8(0.5546875),   // sin(24*(π/128))
    Q_8_8(0.57421875),  // sin(25*(π/128))
    Q_8_8(0.59375),     // sin(26*(π/128))
    Q_8_8(0.61328125),  // sin(27*(π/128))
    Q_8_8(0.6328125),   // sin(28*(π/128))
    Q_8_8(0.65234375),  // sin(29*(π/128))
    Q_8_8(0.66796875),  // sin(30*(π/128))
    Q_8_8(0.6875),      // sin(31*(π/128))
    Q_8_8(0.70703125),  // sin(32*(π/128))
    Q_8_8(0.72265625),  // sin(33*(π/128))
    Q_8_8(0.73828125),  // sin(34*(π/128))
    Q_8_8(0.75390625),  // sin(35*(π/128))
    Q_8_8(0.76953125),  // sin(36*(π/128))
    Q_8_8(0.78515625),  // sin(37*(π/128))
    Q_8_8(0.80078125),  // sin(38*(π/128))
    Q_8_8(0.81640625),  // sin(39*(π/128))
    Q_8_8(0.828125),    // sin(40*(π/128))
    Q_8_8(0.84375),     // sin(41*(π/128))
    Q_8_8(0.85546875),  // sin(42*(π/128))
    Q_8_8(0.8671875),   // sin(43*(π/128))
    Q_8_8(0.87890625),  // sin(44*(π/128))
    Q_8_8(0.890625),    // sin(45*(π/128))
    Q_8_8(0.90234375),  // sin(46*(π/128))
    Q_8_8(0.9140625),   // sin(47*(π/128))
    Q_8_8(0.921875),    // sin(48*(π/128))
    Q_8_8(0.9296875),   // sin(49*(π/128))
    Q_8_8(0.94140625),  // sin(50*(π/128))
    Q_8_8(0.94921875),  // sin(51*(π/128))
    Q_8_8(0.953125),    // sin(52*(π/128))
    Q_8_8(0.9609375),   // sin(53*(π/128))
    Q_8_8(0.96875),     // sin(54*(π/128))
    Q_8_8(0.97265625),  // sin(55*(π/128))
    Q_8_8(0.98046875),  // sin(56*(π/128))
    Q_8_8(0.984375),    // sin(57*(π/128))
    Q_8_8(0.98828125),  // sin(58*(π/128))
    Q_8_8(0.9921875),   // sin(59*(π/128))
    Q_8_8(0.9921875),   // sin(60*(π/128))
    Q_8_8(0.99609375),  // sin(61*(π/128))
    Q_8_8(0.99609375),  // sin(62*(π/128))
    Q_8_8(0.99609375),  // sin(63*(π/128))
    Q_8_8(1),           // sin(64*(π/128))
    Q_8_8(0.99609375),  // sin(65*(π/128))
    Q_8_8(0.99609375),  // sin(66*(π/128))
    Q_8_8(0.99609375),  // sin(67*(π/128))
    Q_8_8(0.9921875),   // sin(68*(π/128))
    Q_8_8(0.9921875),   // sin(69*(π/128))
    Q_8_8(0.98828125),  // sin(70*(π/128))
    Q_8_8(0.984375),    // sin(71*(π/128))
    Q_8_8(0.98046875),  // sin(72*(π/128))
    Q_8_8(0.97265625),  // sin(73*(π/128))
    Q_8_8(0.96875),     // sin(74*(π/128))
    Q_8_8(0.9609375),   // sin(75*(π/128))
    Q_8_8(0.953125),    // sin(76*(π/128))
    Q_8_8(0.94921875),  // sin(77*(π/128))
    Q_8_8(0.94140625),  // sin(78*(π/128))
    Q_8_8(0.9296875),   // sin(79*(π/128))
    Q_8_8(0.921875),    // sin(80*(π/128))
    Q_8_8(0.9140625),   // sin(81*(π/128))
    Q_8_8(0.90234375),  // sin(82*(π/128))
    Q_8_8(0.890625),    // sin(83*(π/128))
    Q_8_8(0.87890625),  // sin(84*(π/128))
    Q_8_8(0.8671875),   // sin(85*(π/128))
    Q_8_8(0.85546875),  // sin(86*(π/128))
    Q_8_8(0.84375),     // sin(87*(π/128))
    Q_8_8(0.828125),    // sin(88*(π/128))
    Q_8_8(0.81640625),  // sin(89*(π/128))
    Q_8_8(0.80078125),  // sin(90*(π/128))
    Q_8_8(0.78515625),  // sin(91*(π/128))
    Q_8_8(0.76953125),  // sin(92*(π/128))
    Q_8_8(0.75390625),  // sin(93*(π/128))
    Q_8_8(0.73828125),  // sin(94*(π/128))
    Q_8_8(0.72265625),  // sin(95*(π/128))
    Q_8_8(0.70703125),  // sin(96*(π/128))
    Q_8_8(0.6875),      // sin(97*(π/128))
    Q_8_8(0.66796875),  // sin(98*(π/128))
    Q_8_8(0.65234375),  // sin(99*(π/128))
    Q_8_8(0.6328125),   // sin(100*(π/128))
    Q_8_8(0.61328125),  // sin(101*(π/128))
    Q_8_8(0.59375),     // sin(102*(π/128))
    Q_8_8(0.57421875),  // sin(103*(π/128))
    Q_8_8(0.5546875),   // sin(104*(π/128))
    Q_8_8(0.53125),     // sin(105*(π/128))
    Q_8_8(0.51171875),  // sin(106*(π/128))
    Q_8_8(0.4921875),   // sin(107*(π/128))
    Q_8_8(0.46875),     // sin(108*(π/128))
    Q_8_8(0.44921875),  // sin(109*(π/128))
    Q_8_8(0.42578125),  // sin(110*(π/128))
    Q_8_8(0.40234375),  // sin(111*(π/128))
    Q_8_8(0.37890625),  // sin(112*(π/128))
    Q_8_8(0.359375),    // sin(113*(π/128))
    Q_8_8(0.3359375),   // sin(114*(π/128))
    Q_8_8(0.3125),      // sin(115*(π/128))
    Q_8_8(0.2890625),   // sin(116*(π/128))
    Q_8_8(0.265625),    // sin(117*(π/128))
    Q_8_8(0.2421875),   // sin(118*(π/128))
    Q_8_8(0.21875),     // sin(119*(π/128))
    Q_8_8(0.19140625),  // sin(120*(π/128))
    Q_8_8(0.16796875),  // sin(121*(π/128))
    Q_8_8(0.14453125),  // sin(122*(π/128))
    Q_8_8(0.12109375),  // sin(123*(π/128))
    Q_8_8(0.09765625),  // sin(124*(π/128))
    Q_8_8(0.0703125),   // sin(125*(π/128))
    Q_8_8(0.046875),    // sin(126*(π/128))
    Q_8_8(0.0234375),   // sin(127*(π/128))
    Q_8_8(0),           // sin(128*(π/128))
    Q_8_8(-0.0234375),  // sin(129*(π/128))
    Q_8_8(-0.046875),   // sin(130*(π/128))
    Q_8_8(-0.0703125),  // sin(131*(π/128))
    Q_8_8(-0.09765625), // sin(132*(π/128))
    Q_8_8(-0.12109375), // sin(133*(π/128))
    Q_8_8(-0.14453125), // sin(134*(π/128))
    Q_8_8(-0.16796875), // sin(135*(π/128))
    Q_8_8(-0.19140625), // sin(136*(π/128))
    Q_8_8(-0.21875),    // sin(137*(π/128))
    Q_8_8(-0.2421875),  // sin(138*(π/128))
    Q_8_8(-0.265625),   // sin(139*(π/128))
    Q_8_8(-0.2890625),  // sin(140*(π/128))
    Q_8_8(-0.3125),     // sin(141*(π/128))
    Q_8_8(-0.3359375),  // sin(142*(π/128))
    Q_8_8(-0.359375),   // sin(143*(π/128))
    Q_8_8(-0.37890625), // sin(144*(π/128))
    Q_8_8(-0.40234375), // sin(145*(π/128))
    Q_8_8(-0.42578125), // sin(146*(π/128))
    Q_8_8(-0.44921875), // sin(147*(π/128))
    Q_8_8(-0.46875),    // sin(148*(π/128))
    Q_8_8(-0.4921875),  // sin(149*(π/128))
    Q_8_8(-0.51171875), // sin(150*(π/128))
    Q_8_8(-0.53125),    // sin(151*(π/128))
    Q_8_8(-0.5546875),  // sin(152*(π/128))
    Q_8_8(-0.57421875), // sin(153*(π/128))
    Q_8_8(-0.59375),    // sin(154*(π/128))
    Q_8_8(-0.61328125), // sin(155*(π/128))
    Q_8_8(-0.6328125),  // sin(156*(π/128))
    Q_8_8(-0.65234375), // sin(157*(π/128))
    Q_8_8(-0.66796875), // sin(158*(π/128))
    Q_8_8(-0.6875),     // sin(159*(π/128))
    Q_8_8(-0.70703125), // sin(160*(π/128))
    Q_8_8(-0.72265625), // sin(161*(π/128))
    Q_8_8(-0.73828125), // sin(162*(π/128))
    Q_8_8(-0.75390625), // sin(163*(π/128))
    Q_8_8(-0.76953125), // sin(164*(π/128))
    Q_8_8(-0.78515625), // sin(165*(π/128))
    Q_8_8(-0.80078125), // sin(166*(π/128))
    Q_8_8(-0.81640625), // sin(167*(π/128))
    Q_8_8(-0.828125),   // sin(168*(π/128))
    Q_8_8(-0.84375),    // sin(169*(π/128))
    Q_8_8(-0.85546875), // sin(170*(π/128))
    Q_8_8(-0.8671875),  // sin(171*(π/128))
    Q_8_8(-0.87890625), // sin(172*(π/128))
    Q_8_8(-0.890625),   // sin(173*(π/128))
    Q_8_8(-0.90234375), // sin(174*(π/128))
    Q_8_8(-0.9140625),  // sin(175*(π/128))
    Q_8_8(-0.921875),   // sin(176*(π/128))
    Q_8_8(-0.9296875),  // sin(177*(π/128))
    Q_8_8(-0.94140625), // sin(178*(π/128))
    Q_8_8(-0.94921875), // sin(179*(π/128))
    Q_8_8(-0.953125),   // sin(180*(π/128))
    Q_8_8(-0.9609375),  // sin(181*(π/128))
    Q_8_8(-0.96875),    // sin(182*(π/128))
    Q_8_8(-0.97265625), // sin(183*(π/128))
    Q_8_8(-0.98046875), // sin(184*(π/128))
    Q_8_8(-0.984375),   // sin(185*(π/128))
    Q_8_8(-0.98828125), // sin(186*(π/128))
    Q_8_8(-0.9921875),  // sin(187*(π/128))
    Q_8_8(-0.9921875),  // sin(188*(π/128))
    Q_8_8(-0.99609375), // sin(189*(π/128))
    Q_8_8(-0.99609375), // sin(190*(π/128))
    Q_8_8(-0.99609375), // sin(191*(π/128))
    Q_8_8(-1),          // sin(192*(π/128))
    Q_8_8(-0.99609375), // sin(193*(π/128))
    Q_8_8(-0.99609375), // sin(194*(π/128))
    Q_8_8(-0.99609375), // sin(195*(π/128))
    Q_8_8(-0.9921875),  // sin(196*(π/128))
    Q_8_8(-0.9921875),  // sin(197*(π/128))
    Q_8_8(-0.98828125), // sin(198*(π/128))
    Q_8_8(-0.984375),   // sin(199*(π/128))
    Q_8_8(-0.98046875), // sin(200*(π/128))
    Q_8_8(-0.97265625), // sin(201*(π/128))
    Q_8_8(-0.96875),    // sin(202*(π/128))
    Q_8_8(-0.9609375),  // sin(203*(π/128))
    Q_8_8(-0.953125),   // sin(204*(π/128))
    Q_8_8(-0.94921875), // sin(205*(π/128))
    Q_8_8(-0.94140625), // sin(206*(π/128))
    Q_8_8(-0.9296875),  // sin(207*(π/128))
    Q_8_8(-0.921875),   // sin(208*(π/128))
    Q_8_8(-0.9140625),  // sin(209*(π/128))
    Q_8_8(-0.90234375), // sin(210*(π/128))
    Q_8_8(-0.890625),   // sin(211*(π/128))
    Q_8_8(-0.87890625), // sin(212*(π/128))
    Q_8_8(-0.8671875),  // sin(213*(π/128))
    Q_8_8(-0.85546875), // sin(214*(π/128))
    Q_8_8(-0.84375),    // sin(215*(π/128))
    Q_8_8(-0.828125),   // sin(216*(π/128))
    Q_8_8(-0.81640625), // sin(217*(π/128))
    Q_8_8(-0.80078125), // sin(218*(π/128))
    Q_8_8(-0.78515625), // sin(219*(π/128))
    Q_8_8(-0.76953125), // sin(220*(π/128))
    Q_8_8(-0.75390625), // sin(221*(π/128))
    Q_8_8(-0.73828125), // sin(222*(π/128))
    Q_8_8(-0.72265625), // sin(223*(π/128))
    Q_8_8(-0.70703125), // sin(224*(π/128))
    Q_8_8(-0.6875),     // sin(225*(π/128))
    Q_8_8(-0.66796875), // sin(226*(π/128))
    Q_8_8(-0.65234375), // sin(227*(π/128))
    Q_8_8(-0.6328125),  // sin(228*(π/128))
    Q_8_8(-0.61328125), // sin(229*(π/128))
    Q_8_8(-0.59375),    // sin(230*(π/128))
    Q_8_8(-0.57421875), // sin(231*(π/128))
    Q_8_8(-0.5546875),  // sin(232*(π/128))
    Q_8_8(-0.53125),    // sin(233*(π/128))
    Q_8_8(-0.51171875), // sin(234*(π/128))
    Q_8_8(-0.4921875),  // sin(235*(π/128))
    Q_8_8(-0.46875),    // sin(236*(π/128))
    Q_8_8(-0.44921875), // sin(237*(π/128))
    Q_8_8(-0.42578125), // sin(238*(π/128))
    Q_8_8(-0.40234375), // sin(239*(π/128))
    Q_8_8(-0.37890625), // sin(240*(π/128))
    Q_8_8(-0.359375),   // sin(241*(π/128))
    Q_8_8(-0.3359375),  // sin(242*(π/128))
    Q_8_8(-0.3125),     // sin(243*(π/128))
    Q_8_8(-0.2890625),  // sin(244*(π/128))
    Q_8_8(-0.265625),   // sin(245*(π/128))
    Q_8_8(-0.2421875),  // sin(246*(π/128))
    Q_8_8(-0.21875),    // sin(247*(π/128))
    Q_8_8(-0.19140625), // sin(248*(π/128))
    Q_8_8(-0.16796875), // sin(249*(π/128))
    Q_8_8(-0.14453125), // sin(250*(π/128))
    Q_8_8(-0.12109375), // sin(251*(π/128))
    Q_8_8(-0.09765625), // sin(252*(π/128))
    Q_8_8(-0.0703125),  // sin(253*(π/128))
    Q_8_8(-0.046875),   // sin(254*(π/128))
    Q_8_8(-0.0234375),  // sin(255*(π/128))
    Q_8_8(0),           // sin(256*(π/128))
    Q_8_8(0.0234375),   // sin(257*(π/128))
    Q_8_8(0.046875),    // sin(258*(π/128))
    Q_8_8(0.0703125),   // sin(259*(π/128))
    Q_8_8(0.09765625),  // sin(260*(π/128))
    Q_8_8(0.12109375),  // sin(261*(π/128))
    Q_8_8(0.14453125),  // sin(262*(π/128))
    Q_8_8(0.16796875),  // sin(263*(π/128))
    Q_8_8(0.19140625),  // sin(264*(π/128))
    Q_8_8(0.21875),     // sin(265*(π/128))
    Q_8_8(0.2421875),   // sin(266*(π/128))
    Q_8_8(0.265625),    // sin(267*(π/128))
    Q_8_8(0.2890625),   // sin(268*(π/128))
    Q_8_8(0.3125),      // sin(269*(π/128))
    Q_8_8(0.3359375),   // sin(270*(π/128))
    Q_8_8(0.359375),    // sin(271*(π/128))
    Q_8_8(0.37890625),  // sin(272*(π/128))
    Q_8_8(0.40234375),  // sin(273*(π/128))
    Q_8_8(0.42578125),  // sin(274*(π/128))
    Q_8_8(0.44921875),  // sin(275*(π/128))
    Q_8_8(0.46875),     // sin(276*(π/128))
    Q_8_8(0.4921875),   // sin(277*(π/128))
    Q_8_8(0.51171875),  // sin(278*(π/128))
    Q_8_8(0.53125),     // sin(279*(π/128))
    Q_8_8(0.5546875),   // sin(280*(π/128))
    Q_8_8(0.57421875),  // sin(281*(π/128))
    Q_8_8(0.59375),     // sin(282*(π/128))
    Q_8_8(0.61328125),  // sin(283*(π/128))
    Q_8_8(0.6328125),   // sin(284*(π/128))
    Q_8_8(0.65234375),  // sin(285*(π/128))
    Q_8_8(0.66796875),  // sin(286*(π/128))
    Q_8_8(0.6875),      // sin(287*(π/128))
    Q_8_8(0.70703125),  // sin(288*(π/128))
    Q_8_8(0.72265625),  // sin(289*(π/128))
    Q_8_8(0.73828125),  // sin(290*(π/128))
    Q_8_8(0.75390625),  // sin(291*(π/128))
    Q_8_8(0.76953125),  // sin(292*(π/128))
    Q_8_8(0.78515625),  // sin(293*(π/128))
    Q_8_8(0.80078125),  // sin(294*(π/128))
    Q_8_8(0.81640625),  // sin(295*(π/128))
    Q_8_8(0.828125),    // sin(296*(π/128))
    Q_8_8(0.84375),     // sin(297*(π/128))
    Q_8_8(0.85546875),  // sin(298*(π/128))
    Q_8_8(0.8671875),   // sin(299*(π/128))
    Q_8_8(0.87890625),  // sin(300*(π/128))
    Q_8_8(0.890625),    // sin(301*(π/128))
    Q_8_8(0.90234375),  // sin(302*(π/128))
    Q_8_8(0.9140625),   // sin(303*(π/128))
    Q_8_8(0.921875),    // sin(304*(π/128))
    Q_8_8(0.9296875),   // sin(305*(π/128))
    Q_8_8(0.94140625),  // sin(306*(π/128))
    Q_8_8(0.94921875),  // sin(307*(π/128))
    Q_8_8(0.953125),    // sin(308*(π/128))
    Q_8_8(0.9609375),   // sin(309*(π/128))
    Q_8_8(0.96875),     // sin(310*(π/128))
    Q_8_8(0.97265625),  // sin(311*(π/128))
    Q_8_8(0.98046875),  // sin(312*(π/128))
    Q_8_8(0.984375),    // sin(313*(π/128))
    Q_8_8(0.98828125),  // sin(314*(π/128))
    Q_8_8(0.9921875),   // sin(315*(π/128))
    Q_8_8(0.9921875),   // sin(316*(π/128))
    Q_8_8(0.99609375),  // sin(317*(π/128))
    Q_8_8(0.99609375),  // sin(318*(π/128))
    Q_8_8(0.99609375),  // sin(319*(π/128))
};

u32 inverseTable[512];

fixed_t float_to_fixed(float n)
{
    return round(n * (1 << 16));
}

/*
// angle from 0 to 65535, where 65536 represents a whole rotation (360deg)
fixed_t fixed_sin(int angle)
{
    angle &= 0xFFFF;  // wrap around
    return float_to_fixed(sin(angle * (2*M_PI) / 65536.0));
}

fixed_t fixed_cos(int angle)
{
    return fixed_sin(angle + 65536 / 4);
}
*/

// angle from 0 to 65535, where 65536 represents a whole rotation (360deg)
fixed_t fixed_sin(int angle)
{
    s32 s;
    angle &= 0xFFFF;  // wrap around
    s = gSineTable[(angle >> 8) & 0xFF];
    return s << 8;
}

fixed_t fixed_cos(int angle)
{
    return fixed_sin(angle + (65536 / 4));
}

// HUD

static char hudText[128];

// must be called during v-blank
static void hud_initialize(void)
{
    unsigned int i;

    // set OAM data
    for (i = 0; i < sizeof(hudText); i++)
    {
        volatile struct OamData *sprite = (struct OamData *)&OAM[i];
        sprite->x = i * 8;
        sprite->y = 0;
        sprite->affineMode = 2;  // disable
        sprite->tileNum = 0x200;
    }

    // Load font into sprite tile memory
    DmaCopy32(3, r6502_portfont_bin, (void *)(VRAM + 0x14000), r6502_portfont_bin_size);

    // Load sprite palette
    u16 *spritePalette = SPRITE_PALETTE;
    spritePalette[0] = RGB5(31, 31, 31);  // transparent
    spritePalette[1] = RGB5(31, 16, 0);   // main color 1
    spritePalette[2] = RGB5(31, 20, 0);   // main color 2
    spritePalette[3] = RGB5(31, 24, 0);   // main color 3
    spritePalette[4] = RGB5(31, 28, 0);   // main color 4
    spritePalette[5] = RGB5(31, 31, 0);   // main color 5
    spritePalette[6] = RGB5(0, 0, 0);     // shadow
}

// must be called during v-blank
static void hud_update(void)
{
    unsigned int i;
    int x = 0;
    int y = 0;

    for (i = 0; i < sizeof(hudText); i++)
    {
        int c = hudText[i];
        volatile struct OamData *sprite = (struct OamData *)&OAM[i];

        if (c == 0)
            break;
        if (c == '\n')
        {
            sprite->affineMode = 2;  // disable
            x = 0;
            y += 8;
            continue;
        }
        sprite->affineMode = 0;  // enable
        sprite->x = x;
        sprite->y = y;
        sprite->tileNum = 0x200 + c - 0x20;
        x += 8;
    }
    // blank tiles
    for (; i < sizeof(hudText); i++)
    {
        volatile struct OamData *sprite = (struct OamData *)&OAM[i];
        sprite->affineMode = 2;  // disable
    }
}

void swap_buffers(void)
{
    fbNum ^= 1;
    if (fbNum == 0)
    {
        frameBuffer = (void *)(VRAM + 0xA000);
        REG_DISPCNT &= ~(1 << 4);
    }
    else
    {
        frameBuffer = (void *)(VRAM);
        REG_DISPCNT |= 1 << 4;
    }
}

static volatile int frames = 0;
static volatile int fps = 0;
static volatile int vblankCount = 0;
static volatile int renderTime = 0;

static void vblank_handler(void)
{
    if (++vblankCount == 60)
    {
        vblankCount = 0;
        fps = frames;
        frames = 0;
    }
    sprintf(hudText, "pos: %i,%i,%i\n"
                     "FPS: %i\n"
                     "Render time: %i cycles",
                     (int)(camera.x >> 16), (int)(camera.y >> 16), (int)camera.height,
                     fps,
                     renderTime);
    hud_update();
}

void vblank_busy_wait(void)
{
    while (!(REG_DISPSTAT & 1))
        ;
}

void initialize(void)
{
    int i;

    // the vblank interrupt must be enabled for VBlankIntrWait() to work
    // since the default dispatcher handles the bios flags no vblank handler
    // is required
    irqInit();
    // interrupts interfere with profiling, so disable them
    //irqSet(IRQ_VBLANK, vblank_handler);
    //irqEnable(IRQ_VBLANK);

    // Set registers
    REG_DISPCNT = DISPCNT_MODE_4 | DISPCNT_BG2_ON | DISPCNT_OBJ_ON;

    // Load palette
    memcpy((void *)BG_PALETTE, colormapPal, 256 * sizeof(u16));


    // Compute fixed point inverses
    for (i = 1; i < 512; i++)
        inverseTable[i] = (1 << 16) / (u32)i;
    
    //VBlankIntrWait();
    vblank_busy_wait();
    hud_initialize();
}

void read_input(void)
{
    input.prevKeys = input.keysDown;
    input.keysDown = ~REG_KEYINPUT;
    input.newKeys = input.keysDown & (input.prevKeys ^ input.keysDown);
}

void update(void)
{
    int vert = 0;
    int horiz = 0;
    int forward = 0;

    if (input.keysDown & KEY_LEFT)
        horiz = -1000;
    if (input.keysDown & KEY_RIGHT)
        horiz = +1000;
    if (input.keysDown & KEY_UP)
        vert = -10;
    if (input.keysDown & KEY_DOWN)
        vert = 10;
    if (input.keysDown & A_BUTTON)
        forward = 1;

    camera.yaw -= horiz;
    camera.sinYaw = fixed_sin(camera.yaw);
    camera.cosYaw = fixed_cos(camera.yaw);
    camera.x -= forward * camera.sinYaw * 4;
    camera.y -= forward * camera.cosYaw * 4;
    camera.horizon -= vert;
    camera.height += forward * (camera.horizon - 100) / 16;
}

static inline void draw_vertical_bar(int x, int top, int bottom, u8 color)
{
    int y;
    //if (top < 0)
    //    top = 0;
    //if (bottom < top)
   //     return;
    //if (bottom > 160)
    //    bottom = 160;
    //assert(bottom <= 160);

    u16 *dest = (u16 *)frameBuffer + top * SCREEN_WIDTH/2 + x;
    for (y = top; y < bottom; y++)
    {
        *dest = color | (color << 8);
        dest += SCREEN_WIDTH/2;
    }
}

#define BG_COLOR 251

__attribute__((section(".iwram"), target("arm"), long_call))
void render_c(void)
{
    int i;
    u8 ybuffer[SCREEN_WIDTH/2];

    DmaFill32(3, BG_COLOR|(BG_COLOR<<8)|(BG_COLOR<<16)|(BG_COLOR<<24), frameBuffer, 160 * 240);

    for (i = 0; i < SCREEN_WIDTH/2; i++)
        ybuffer[i] = 160;

    fixed_t s = camera.sinYaw;
    fixed_t c = camera.cosYaw;
    const u32 drawdistance = 512;
    u32 z;
    for (z = 1; z < drawdistance;)
    {
        fixed_t lx = (-c * z - s * z);
        fixed_t ly = (s * z - c * z);
        fixed_t rx = (c * z - s * z);
        fixed_t ry = (-s * z - c * z);
        /*
        fixed_t dx = (rx - lx) / 240;
        fixed_t dy = (ry - ly) / 240;
        */
        // this is less accurate, but faster
        fixed_t dx = (rx - lx) >> 8;
        fixed_t dy = (ry - ly) >> 8;

        dx *= 2;
        dy *= 2;

        lx += (camera.x);
        ly += (camera.y);

        //fixed_t invz = 65536 / z;
        fixed_t invz = inverseTable[z];

        for (i = 0; i < SCREEN_WIDTH/2; i++, ly += dy, lx += dx)
        {
            u32 index = ((ly >> 16) & 1023) * 1024 + ((lx >> 16) & 1023);
            /*
            u32 index2 = ((ly >> 15) & (1023<<1)) * 1024 + ((lx >> 15) & (1023<<1));
            assert(index2 == index * 2);
            */
            //if ((u32)ly >= 2*1024 << 16 || (u32)lx >= 2*1024 << 16) continue; // bounds
            s32 height = ((128 * (camera.height - terrain_bin[index * 2 + 1]) * invz) >> 16) + camera.horizon;
            if (height < 0)
                height = 0;
            if (height < ybuffer[i])
            {
                u8 color = terrain_bin[index * 2];
                draw_vertical_bar(i, height, ybuffer[i], color);
                ybuffer[i] = height;
            }
        }
        if (z >= 256)
            z += 8;
        if (z >= 128)
            z += 4;
        else
            z += 2;
    }
}

extern __attribute__((section(".iwram"), target("arm"), long_call)) void render_asm(void);

//---------------------------------------------------------------------------------
// Program entry point
//---------------------------------------------------------------------------------
int main(void)
{
//---------------------------------------------------------------------------------
    frameBuffer = (void *)(VRAM + 0xA000);
    fbNum = 0;
    initialize();

    camera.sinYaw = fixed_sin(camera.yaw);
    camera.cosYaw = fixed_cos(camera.yaw);
    camera.x = 512<<16;
    camera.y = 800<<16;
    camera.height = 70;
    camera.yaw = 0;
    camera.horizon = 100;

    while (1) {
        read_input();
        update();
        //render_c();
        REG_TM0CNT_L = 0;  // count from 0
        REG_TM0CNT_H = (1 << 7);  // enable timer
        render_asm();
        renderTime = REG_TM0CNT_L;  // read time
        REG_TM0CNT_H = 0;  // disable timer
        
        renderTime = REG_TM0CNT_L;
        frames++;
        sprintf(hudText,
            "position: %i, %i, %i\n"
            "render time: %i cycles\n",
            (int)(camera.x >> 16), (int)(camera.y >> 16), (int)camera.height,
            renderTime);
        //VBlankIntrWait();
        vblank_busy_wait();
        hud_update();
        swap_buffers();
    }
}


