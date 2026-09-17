#include "theme.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "actions.hpp"
#include "fonts.hpp"

namespace {
struct Def { ThemeColor id; const char* name; ImVec4 rgba; };

const Def kDefaults[] = {
    {TC_PATTERN_BG,       "Pattern background",     {0.07f, 0.04f, 0.06f, 1.0f}},
    {TC_ROW_INDEX,        "Row number",             {0.90f, 0.50f, 0.70f, 1.0f}},
    {TC_ROW_INDEX_HI1,    "Row number (hi 1)",      {1.00f, 0.65f, 0.82f, 1.0f}},
    {TC_ROW_INDEX_HI2,    "Row number (hi 2)",      {0.70f, 1.00f, 0.80f, 1.0f}},
    {TC_ROW_INDEX_PLAYING,"Row number (playing)",   {1.00f, 1.00f, 1.00f, 1.0f}},
    {TC_ROW_HI1,          "Row highlight 1",        {1.00f, 0.45f, 0.65f, 0.12f}},
    {TC_ROW_HI2,          "Row highlight 2",        {0.40f, 1.00f, 0.60f, 0.16f}},
    {TC_ROW_CURSOR,       "Cursor row tint",        {1.00f, 0.60f, 0.75f, 0.06f}},
    {TC_PLAYHEAD,         "Playhead",               {1.00f, 0.55f, 0.70f, 0.22f}},
    {TC_PLAYHEAD_LINE,    "Playhead line",          {1.00f, 0.35f, 0.60f, 0.95f}},
    {TC_CURSOR,           "Cursor",                 {0.70f, 0.15f, 0.32f, 0.85f}},
    {TC_CURSOR_EDIT,      "Cursor (edit mode)",     {0.95f, 0.20f, 0.25f, 0.90f}},
    {TC_SELECTION,        "Selection",              {0.85f, 0.30f, 0.55f, 0.45f}},
    {TC_NOTE,             "Note",                   {1.00f, 0.93f, 0.96f, 1.0f}},
    {TC_BLANK,            "Blank",                  {0.46f, 0.34f, 0.40f, 1.0f}},
    {TC_NOTE_OFF,         "Note off (rest)",        {1.00f, 0.40f, 0.45f, 1.0f}},
    {TC_NOTE_TIE,         "Tie",                    {0.95f, 0.62f, 0.82f, 1.0f}},
    {TC_NOTE_PERC,        "Percussion note",        {1.00f, 0.72f, 0.62f, 1.0f}},
    {TC_NOTE_SUB,         "Subroutine (read-only)", {0.62f, 0.85f, 0.72f, 1.0f}},
    {TC_INS,              "Instrument",             {0.45f, 0.95f, 0.60f, 1.0f}},
    {TC_INS_INVALID,      "Instrument (out of range)", {1.00f, 0.20f, 0.20f, 1.0f}},
    {TC_QUANT,            "Quantise",               {0.65f, 0.80f, 0.68f, 1.0f}},
    {TC_VOL_MAX,          "Velocity (max)",         {0.55f, 1.00f, 0.60f, 1.0f}},
    {TC_VOL_HALF,         "Velocity (half)",        {0.35f, 0.80f, 0.45f, 1.0f}},
    {TC_VOL_MIN,          "Velocity (min)",         {0.25f, 0.52f, 0.35f, 1.0f}},
    {TC_FX_INVALID,       "Effect: unknown",        {1.00f, 0.20f, 0.20f, 1.0f}},
    {TC_FX_PITCH,         "Effect: pitch",          {1.00f, 0.60f, 0.85f, 1.0f}},
    {TC_FX_VOLUME,        "Effect: volume",         {0.40f, 1.00f, 0.50f, 1.0f}},
    {TC_FX_PANNING,       "Effect: panning",        {0.60f, 1.00f, 0.82f, 1.0f}},
    {TC_FX_SONG,          "Effect: song / flow",    {1.00f, 0.30f, 0.32f, 1.0f}},
    {TC_FX_TIME,          "Effect: time",           {0.92f, 0.50f, 0.62f, 1.0f}},
    {TC_FX_SPEED,         "Effect: tempo",          {1.00f, 0.30f, 0.70f, 1.0f}},
    {TC_FX_SYS1,          "Effect: echo / DSP",     {0.72f, 1.00f, 0.40f, 1.0f}},
    {TC_FX_SYS2,          "Effect: driver specific",{0.30f, 0.90f, 0.70f, 1.0f}},
    {TC_FX_MISC,          "Effect: misc",           {0.80f, 0.65f, 0.72f, 1.0f}},
    {TC_OFFGRID,          "Off-grid marker",        {0.95f, 0.55f, 0.70f, 1.0f}},
    {TC_EXTRA,            "More events marker",     {1.00f, 1.00f, 1.00f, 1.0f}},
    {TC_CHANNEL_HEADER,   "Channel header",         {1.00f, 0.90f, 0.94f, 1.0f}},
    {TC_CHANNEL_HEADER_BG,"Channel header bar",     {0.52f, 0.14f, 0.26f, 1.0f}},
    {TC_CHANNEL_MUTED,    "Channel header (muted)", {0.32f, 0.18f, 0.22f, 1.0f}},
    {TC_CHANNEL_SOLO,     "Channel header (solo)",  {0.35f, 0.75f, 0.45f, 1.0f}},
    {TC_CHANNEL_CURSOR,   "Channel header (cursor)",{0.85f, 0.30f, 0.52f, 1.0f}},
    {TC_ORDER_INDEX,      "Order number",           {0.90f, 0.50f, 0.70f, 1.0f}},
    {TC_ORDER_PLAYING,    "Order (playing)",        {0.45f, 1.00f, 0.60f, 0.30f}},
    {TC_ORDER_SELECTED,   "Order (selected)",       {1.00f, 0.50f, 0.70f, 0.55f}},
    {TC_ORDER_SIMILAR,    "Order (same pattern)",   {0.55f, 1.00f, 0.75f, 1.0f}},
    {TC_METER_LOW,        "Level meter (low)",      {0.35f, 0.85f, 0.45f, 1.0f}},
    {TC_METER_HIGH,       "Level meter (high)",     {1.00f, 0.35f, 0.55f, 1.0f}},
    {TC_STATUS_EDIT,      "Status: edit mode",      {1.00f, 0.32f, 0.38f, 1.0f}},
    {TC_STATUS_PLAY,      "Status: playing",        {0.50f, 1.00f, 0.60f, 1.0f}},
};
const Def kClassicTracker[] = {
    {TC_PATTERN_BG,       "Pattern background",     {0.06f, 0.06f, 0.08f, 1.0f}},
    {TC_ROW_INDEX,        "Row number",             {0.50f, 0.80f, 1.00f, 1.0f}},
    {TC_ROW_INDEX_HI1,    "Row number (hi 1)",      {0.60f, 0.90f, 1.00f, 1.0f}},
    {TC_ROW_INDEX_HI2,    "Row number (hi 2)",      {0.80f, 1.00f, 1.00f, 1.0f}},
    {TC_ROW_INDEX_PLAYING,"Row number (playing)",   {1.00f, 1.00f, 1.00f, 1.0f}},
    {TC_ROW_HI1,          "Row highlight 1",        {0.60f, 0.60f, 0.60f, 0.15f}},
    {TC_ROW_HI2,          "Row highlight 2",        {0.50f, 0.80f, 1.00f, 0.20f}},
    {TC_ROW_CURSOR,       "Cursor row tint",        {1.00f, 1.00f, 1.00f, 0.05f}},
    {TC_PLAYHEAD,         "Playhead",               {1.00f, 1.00f, 1.00f, 0.22f}},
    {TC_PLAYHEAD_LINE,    "Playhead line",          {1.00f, 0.90f, 0.40f, 0.90f}},
    {TC_CURSOR,           "Cursor",                 {0.20f, 0.45f, 0.70f, 0.85f}},
    {TC_CURSOR_EDIT,      "Cursor (edit mode)",     {0.80f, 0.25f, 0.25f, 0.85f}},
    {TC_SELECTION,        "Selection",              {0.25f, 0.30f, 0.50f, 0.55f}},
    {TC_NOTE,             "Note",                   {1.00f, 1.00f, 1.00f, 1.0f}},
    {TC_BLANK,            "Blank",                  {0.42f, 0.42f, 0.42f, 1.0f}},
    {TC_NOTE_OFF,         "Note off (rest)",        {1.00f, 0.55f, 0.55f, 1.0f}},
    {TC_NOTE_TIE,         "Tie",                    {0.60f, 0.60f, 0.90f, 1.0f}},
    {TC_NOTE_PERC,        "Percussion note",        {1.00f, 0.80f, 0.55f, 1.0f}},
    {TC_NOTE_SUB,         "Subroutine (read-only)", {0.55f, 0.65f, 0.90f, 1.0f}},
    {TC_INS,              "Instrument",             {0.40f, 0.70f, 1.00f, 1.0f}},
    {TC_INS_INVALID,      "Instrument (out of range)", {1.00f, 0.20f, 0.20f, 1.0f}},
    {TC_QUANT,            "Quantise",               {0.55f, 0.75f, 0.60f, 1.0f}},
    {TC_VOL_MAX,          "Velocity (max)",         {0.00f, 1.00f, 0.00f, 1.0f}},
    {TC_VOL_HALF,         "Velocity (half)",        {0.00f, 0.75f, 0.00f, 1.0f}},
    {TC_VOL_MIN,          "Velocity (min)",         {0.00f, 0.50f, 0.00f, 1.0f}},
    {TC_FX_INVALID,       "Effect: unknown",        {1.00f, 0.20f, 0.20f, 1.0f}},
    {TC_FX_PITCH,         "Effect: pitch",          {1.00f, 1.00f, 0.00f, 1.0f}},
    {TC_FX_VOLUME,        "Effect: volume",         {0.00f, 1.00f, 0.00f, 1.0f}},
    {TC_FX_PANNING,       "Effect: panning",        {0.00f, 1.00f, 1.00f, 1.0f}},
    {TC_FX_SONG,          "Effect: song / flow",    {1.00f, 0.35f, 0.35f, 1.0f}},
    {TC_FX_TIME,          "Effect: time",           {0.60f, 0.30f, 1.00f, 1.0f}},
    {TC_FX_SPEED,         "Effect: tempo",          {1.00f, 0.00f, 1.00f, 1.0f}},
    {TC_FX_SYS1,          "Effect: echo / DSP",     {0.50f, 1.00f, 0.00f, 1.0f}},
    {TC_FX_SYS2,          "Effect: driver specific",{0.00f, 1.00f, 0.50f, 1.0f}},
    {TC_FX_MISC,          "Effect: misc",           {0.75f, 0.75f, 0.75f, 1.0f}},
    {TC_OFFGRID,          "Off-grid marker",        {0.80f, 0.60f, 0.30f, 1.0f}},
    {TC_EXTRA,            "More events marker",     {1.00f, 1.00f, 1.00f, 1.0f}},
    {TC_CHANNEL_HEADER,   "Channel header",         {0.85f, 0.85f, 0.95f, 1.0f}},
    {TC_CHANNEL_HEADER_BG,"Channel header bar",     {0.20f, 0.35f, 0.55f, 1.0f}},
    {TC_CHANNEL_MUTED,    "Channel header (muted)", {0.45f, 0.30f, 0.30f, 1.0f}},
    {TC_CHANNEL_SOLO,     "Channel header (solo)",  {1.00f, 0.85f, 0.40f, 1.0f}},
    {TC_CHANNEL_CURSOR,   "Channel header (cursor)",{0.30f, 0.55f, 0.85f, 1.0f}},
    {TC_ORDER_INDEX,      "Order number",           {0.50f, 0.80f, 1.00f, 1.0f}},
    {TC_ORDER_PLAYING,    "Order (playing)",        {0.40f, 0.70f, 1.00f, 0.30f}},
    {TC_ORDER_SELECTED,   "Order (selected)",       {0.60f, 0.80f, 1.00f, 0.60f}},
    {TC_ORDER_SIMILAR,    "Order (same pattern)",   {0.50f, 1.00f, 1.00f, 1.0f}},
    {TC_METER_LOW,        "Level meter (low)",      {0.20f, 0.70f, 0.30f, 1.0f}},
    {TC_METER_HIGH,       "Level meter (high)",     {1.00f, 0.80f, 0.20f, 1.0f}},
    {TC_STATUS_EDIT,      "Status: edit mode",      {1.00f, 0.35f, 0.35f, 1.0f}},
    {TC_STATUS_PLAY,      "Status: playing",        {0.40f, 1.00f, 0.50f, 1.0f}},
};
const Def kEnby[] = {
    {TC_PATTERN_BG,       "Pattern background",     {0.08f, 0.07f, 0.10f, 1.0f}},
    {TC_ROW_INDEX,        "Row number",             {0.61f, 0.42f, 0.80f, 1.0f}},
    {TC_ROW_INDEX_HI1,    "Row number (hi 1)",      {0.78f, 0.60f, 0.95f, 1.0f}},
    {TC_ROW_INDEX_HI2,    "Row number (hi 2)",      {0.99f, 0.96f, 0.35f, 1.0f}},
    {TC_ROW_INDEX_PLAYING,"Row number (playing)",   {1.00f, 1.00f, 1.00f, 1.0f}},
    {TC_ROW_HI1,          "Row highlight 1",        {0.61f, 0.35f, 0.82f, 0.16f}},
    {TC_ROW_HI2,          "Row highlight 2",        {0.99f, 0.96f, 0.20f, 0.14f}},
    {TC_ROW_CURSOR,       "Cursor row tint",        {0.80f, 0.65f, 1.00f, 0.06f}},
    {TC_PLAYHEAD,         "Playhead",               {0.99f, 0.96f, 0.35f, 0.20f}},
    {TC_PLAYHEAD_LINE,    "Playhead line",          {0.99f, 0.96f, 0.20f, 0.95f}},
    {TC_CURSOR,           "Cursor",                 {0.50f, 0.25f, 0.75f, 0.85f}},
    {TC_CURSOR_EDIT,      "Cursor (edit mode)",     {0.90f, 0.85f, 0.15f, 0.85f}},
    {TC_SELECTION,        "Selection",              {0.61f, 0.35f, 0.82f, 0.45f}},
    {TC_NOTE,             "Note",                   {1.00f, 1.00f, 1.00f, 1.0f}},
    {TC_BLANK,            "Blank",                  {0.36f, 0.30f, 0.44f, 1.0f}},
    {TC_NOTE_OFF,         "Note off (rest)",        {0.72f, 0.55f, 0.90f, 1.0f}},
    {TC_NOTE_TIE,         "Tie",                    {0.62f, 0.50f, 0.78f, 1.0f}},
    {TC_NOTE_PERC,        "Percussion note",        {0.99f, 0.90f, 0.55f, 1.0f}},
    {TC_NOTE_SUB,         "Subroutine (read-only)", {0.55f, 0.45f, 0.70f, 1.0f}},
    {TC_INS,              "Instrument",             {0.99f, 0.96f, 0.35f, 1.0f}},
    {TC_INS_INVALID,      "Instrument (out of range)", {1.00f, 0.30f, 0.30f, 1.0f}},
    {TC_QUANT,            "Quantise",               {0.80f, 0.70f, 0.95f, 1.0f}},
    {TC_VOL_MAX,          "Velocity (max)",         {1.00f, 0.98f, 0.45f, 1.0f}},
    {TC_VOL_HALF,         "Velocity (half)",        {0.80f, 0.75f, 0.30f, 1.0f}},
    {TC_VOL_MIN,          "Velocity (min)",         {0.50f, 0.45f, 0.25f, 1.0f}},
    {TC_FX_INVALID,       "Effect: unknown",        {1.00f, 0.30f, 0.30f, 1.0f}},
    {TC_FX_PITCH,         "Effect: pitch",          {0.85f, 0.65f, 1.00f, 1.0f}},
    {TC_FX_VOLUME,        "Effect: volume",         {0.99f, 0.96f, 0.35f, 1.0f}},
    {TC_FX_PANNING,       "Effect: panning",        {0.95f, 0.92f, 0.70f, 1.0f}},
    {TC_FX_SONG,          "Effect: song / flow",    {1.00f, 1.00f, 1.00f, 1.0f}},
    {TC_FX_TIME,          "Effect: time",           {0.70f, 0.45f, 0.90f, 1.0f}},
    {TC_FX_SPEED,         "Effect: tempo",          {0.99f, 0.80f, 0.30f, 1.0f}},
    {TC_FX_SYS1,          "Effect: echo / DSP",     {0.60f, 0.35f, 0.82f, 1.0f}},
    {TC_FX_SYS2,          "Effect: driver specific",{0.85f, 0.80f, 0.55f, 1.0f}},
    {TC_FX_MISC,          "Effect: misc",           {0.70f, 0.65f, 0.78f, 1.0f}},
    {TC_OFFGRID,          "Off-grid marker",        {0.99f, 0.85f, 0.40f, 1.0f}},
    {TC_EXTRA,            "More events marker",     {1.00f, 1.00f, 1.00f, 1.0f}},
    {TC_CHANNEL_HEADER,   "Channel header",         {1.00f, 1.00f, 1.00f, 1.0f}},
    {TC_CHANNEL_HEADER_BG,"Channel header bar",     {0.40f, 0.22f, 0.58f, 1.0f}},
    {TC_CHANNEL_MUTED,    "Channel header (muted)", {0.32f, 0.28f, 0.38f, 1.0f}},
    {TC_CHANNEL_SOLO,     "Channel header (solo)",  {0.99f, 0.96f, 0.35f, 1.0f}},
    {TC_CHANNEL_CURSOR,   "Channel header (cursor)",{0.66f, 0.40f, 0.88f, 1.0f}},
    {TC_ORDER_INDEX,      "Order number",           {0.72f, 0.55f, 0.90f, 1.0f}},
    {TC_ORDER_PLAYING,    "Order (playing)",        {0.99f, 0.96f, 0.35f, 0.30f}},
    {TC_ORDER_SELECTED,   "Order (selected)",       {0.61f, 0.35f, 0.82f, 0.55f}},
    {TC_ORDER_SIMILAR,    "Order (same pattern)",   {0.95f, 0.92f, 0.70f, 1.0f}},
    {TC_METER_LOW,        "Level meter (low)",      {0.61f, 0.35f, 0.82f, 1.0f}},
    {TC_METER_HIGH,       "Level meter (high)",     {0.99f, 0.96f, 0.20f, 1.0f}},
    {TC_STATUS_EDIT,      "Status: edit mode",      {0.99f, 0.96f, 0.35f, 1.0f}},
    {TC_STATUS_PLAY,      "Status: playing",        {0.80f, 0.60f, 1.00f, 1.0f}},
};
static_assert(sizeof kEnby / sizeof kEnby[0] == TC_COUNT, "enby table out of sync");
static_assert(sizeof kDefaults / sizeof kDefaults[0] == TC_COUNT, "theme table out of sync");
static_assert(sizeof kClassicTracker / sizeof kClassicTracker[0] == TC_COUNT, "classic table out of sync");

Theme g_theme;

int g_capture_action = -1, g_capture_slot = 0;

}

Theme::Theme() { reset_default(); }

void Theme::reset_default() {
    for (const Def& d : kDefaults) { colors[d.id] = d.rgba; names[d.id] = d.name; }
}

void Theme::preset(int which) {
    reset_default();
    if (which == 4) {
        for (const Def& d : kClassicTracker) colors[d.id] = d.rgba;
        return;
    }
    if (which == 5) {
        for (const Def& d : kEnby) colors[d.id] = d.rgba;
        return;
    }
    if (which == 1) {
        colors[TC_PATTERN_BG]   = {0.02f, 0.03f, 0.12f, 1};
        colors[TC_NOTE]         = {0.95f, 0.95f, 1.00f, 1};
        colors[TC_BLANK]        = {0.35f, 0.40f, 0.60f, 1};
        colors[TC_ROW_HI1]      = {0.30f, 0.40f, 0.80f, 0.20f};
        colors[TC_ROW_HI2]      = {0.40f, 0.50f, 1.00f, 0.30f};
        colors[TC_INS]          = {0.55f, 0.85f, 1.00f, 1};
        colors[TC_CURSOR]       = {0.30f, 0.55f, 0.95f, 0.9f};
        colors[TC_ROW_INDEX]    = {0.70f, 0.75f, 1.00f, 1};
        colors[TC_CHANNEL_HEADER_BG] = {0.15f, 0.25f, 0.60f, 1};
    } else if (which == 2) {
        colors[TC_PATTERN_BG]   = {0.05f, 0.04f, 0.02f, 1};
        colors[TC_NOTE]         = {1.00f, 0.80f, 0.35f, 1};
        colors[TC_BLANK]        = {0.40f, 0.30f, 0.15f, 1};
        colors[TC_NOTE_OFF]     = {0.75f, 0.45f, 0.25f, 1};
        colors[TC_NOTE_TIE]     = {0.80f, 0.60f, 0.30f, 1};
        colors[TC_ROW_INDEX]    = {0.90f, 0.65f, 0.25f, 1};
        colors[TC_ROW_INDEX_HI1]= {1.00f, 0.75f, 0.30f, 1};
        colors[TC_ROW_INDEX_HI2]= {1.00f, 0.90f, 0.50f, 1};
        colors[TC_ROW_HI1]      = {1.00f, 0.70f, 0.20f, 0.10f};
        colors[TC_ROW_HI2]      = {1.00f, 0.80f, 0.30f, 0.18f};
        colors[TC_INS]          = {1.00f, 0.90f, 0.60f, 1};
        colors[TC_QUANT]        = {0.85f, 0.65f, 0.35f, 1};
        colors[TC_CURSOR]       = {0.80f, 0.50f, 0.10f, 0.85f};
        colors[TC_SELECTION]    = {0.60f, 0.40f, 0.10f, 0.5f};
        colors[TC_PLAYHEAD]     = {1.00f, 0.85f, 0.50f, 0.20f};
        colors[TC_CHANNEL_HEADER_BG] = {0.45f, 0.30f, 0.10f, 1};
        colors[TC_CHANNEL_HEADER]    = {1.00f, 0.90f, 0.70f, 1};
    } else if (which == 3) {
        colors[TC_PATTERN_BG]   = {0.09f, 0.09f, 0.09f, 1};
        colors[TC_ROW_HI1]      = {0.60f, 0.60f, 0.60f, 0.20f};
        colors[TC_ROW_HI2]      = {0.50f, 0.80f, 1.00f, 0.20f};
        colors[TC_CURSOR]       = {0.10f, 0.30f, 0.50f, 0.90f};
        colors[TC_CURSOR_EDIT]  = {0.10f, 0.30f, 0.50f, 0.90f};
        colors[TC_SELECTION]    = {0.15f, 0.15f, 0.50f, 0.70f};
        colors[TC_NOTE]         = {1.00f, 1.00f, 1.00f, 1};
        colors[TC_BLANK]        = {0.30f, 0.30f, 0.30f, 1};
        colors[TC_NOTE_OFF]     = {1.00f, 0.50f, 0.50f, 1};
        colors[TC_NOTE_TIE]     = {0.50f, 0.50f, 1.00f, 1};
        colors[TC_INS]          = {0.40f, 0.70f, 1.00f, 1};
        colors[TC_QUANT]        = {0.00f, 0.75f, 0.00f, 1};
        colors[TC_FX_MISC]      = {0.30f, 0.30f, 0.30f, 1};
        colors[TC_CHANNEL_HEADER_BG] = {0.27f, 0.27f, 0.27f, 1};
        colors[TC_CHANNEL_HEADER]    = {1.00f, 1.00f, 1.00f, 1};
        colors[TC_CHANNEL_MUTED]     = {0.50f, 0.50f, 0.50f, 1};
        colors[TC_CHANNEL_SOLO]      = {1.00f, 0.75f, 0.30f, 1};
        colors[TC_PLAYHEAD]     = {1.00f, 1.00f, 1.00f, 0.25f};
    }
}

ImU32 Theme::u32(ThemeColor c, float alpha_mul) const {
    ImVec4 v = colors[c];
    v.w *= alpha_mul;
    return ImGui::ColorConvertFloat4ToU32(v);
}

ImU32 Theme::volume(float t) const {
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    const ImVec4& a = t < 0.5f ? colors[TC_VOL_MIN] : colors[TC_VOL_HALF];
    const ImVec4& b = t < 0.5f ? colors[TC_VOL_HALF] : colors[TC_VOL_MAX];
    float k = t < 0.5f ? t * 2 : (t - 0.5f) * 2;
    ImVec4 c(a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(c);
}

ImU32 Theme::meter(float t) const {
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float h0, s0, v0, h1, s1, v1;
    const ImVec4& lo = colors[TC_METER_LOW];
    const ImVec4& hi = colors[TC_METER_HIGH];
    ImGui::ColorConvertRGBtoHSV(lo.x, lo.y, lo.z, h0, s0, v0);
    ImGui::ColorConvertRGBtoHSV(hi.x, hi.y, hi.z, h1, s1, v1);
    if (h1 > h0) h1 -= 1.0f;
    float h = h0 + (h1 - h0) * t;
    if (h < 0) h += 1.0f;
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(h, s0 + (s1 - s0) * t, v0 + (v1 - v0) * t, r, g, b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 1));
}

ImU32 Theme::instrument(int n) const {
    if (!ins_colors || n < 0) return u32(TC_INS);
    float h = std::fmod(float(n) * 0.618034f + 0.55f, 1.0f);
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(h, 0.55f, 1.0f, r, g, b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 1));
}

bool Theme::load(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) return false;
    char line[512];
    while (std::fgets(line, sizeof line, f)) {
        char key[64], sv[256]; float r, g, b, a, fv; int iv;
        if (std::sscanf(line, "color.%63[^=]=%f,%f,%f,%f", key, &r, &g, &b, &a) == 5) {
            for (int i = 0; i < TC_COUNT; ++i)
                if (!std::strcmp(key, kDefaults[i].name)) colors[kDefaults[i].id] = ImVec4(r, g, b, a);
        } else if (std::sscanf(line, "row_hi1=%d", &iv) == 1) row_hi1 = iv;
        else if (std::sscanf(line, "row_hi2=%d", &iv) == 1) row_hi2 = iv;
        else if (std::sscanf(line, "hex_rows=%d", &iv) == 1) hex_rows = iv;
        else if (std::sscanf(line, "center_playhead=%d", &iv) == 1) follow_mode = iv ? 1 : 2;
        else if (std::sscanf(line, "follow_mode=%d", &iv) == 1) follow_mode = iv;
        else if (std::sscanf(line, "playhead_pos=%f", &fv) == 1) playhead_pos = fv;
        else if (std::sscanf(line, "latency_ms=%d", &iv) == 1) latency_ms = iv;
        else if (std::sscanf(line, "edit_step=%d", &iv) == 1) edit_step = iv;
        else if (std::sscanf(line, "ins_colors=%d", &iv) == 1) ins_colors = iv;
        else if (std::sscanf(line, "fx_hex_codes=%d", &iv) == 1) fx_hex_codes = iv;
        else if (std::sscanf(line, "dim_muted=%d", &iv) == 1) dim_muted = iv;
        else if (std::sscanf(line, "show_meters=%d", &iv) == 1) show_meters = iv;
        else if (std::sscanf(line, "cursor_row_tint=%d", &iv) == 1) cursor_row_tint = iv;
        else if (std::sscanf(line, "wrap_cursor=%d", &iv) == 1) wrap_cursor = iv;
        else if (std::sscanf(line, "step_on_hex=%d", &iv) == 1) step_on_hex = iv;
        else if (std::sscanf(line, "note_writes_ins=%d", &iv) == 1) note_writes_ins = iv;
        else if (std::sscanf(line, "font_size_ui=%f", &fv) == 1) font_size_ui = fv;
        else if (std::sscanf(line, "font_tracking=%f", &fv) == 1) font_tracking = fv;
        else if (std::sscanf(line, "font_size_pattern=%f", &fv) == 1) font_size_pattern = fv;
        else if (std::sscanf(line, "widget_gap=%f", &fv) == 1) widget_gap = fv;
        else if (std::sscanf(line, "font_ui=%255[^\n]", sv) == 1) std::snprintf(font_ui, sizeof font_ui, "%s", sv);
        else if (std::sscanf(line, "last_dir=%255[^\n]", sv) == 1) std::snprintf(last_dir, sizeof last_dir, "%s", sv);
        else if (std::sscanf(line, "font_mono=%255[^\n]", sv) == 1) std::snprintf(font_mono, sizeof font_mono, "%s", sv);
    }
    std::fclose(f);
    return true;
}

bool Theme::save(const char* path) const {
    FILE* f = std::fopen(path, "w");
    if (!f) return false;
    for (const Def& d : kDefaults) {
        const ImVec4& c = colors[d.id];
        std::fprintf(f, "color.%s=%.3f,%.3f,%.3f,%.3f\n", d.name, c.x, c.y, c.z, c.w);
    }
    std::fprintf(f, "row_hi1=%d\nrow_hi2=%d\nhex_rows=%d\nfollow_mode=%d\nplayhead_pos=%.2f\nlatency_ms=%d\nedit_step=%d\n",
                 row_hi1, row_hi2, hex_rows, follow_mode, playhead_pos, latency_ms, edit_step);
    std::fprintf(f, "ins_colors=%d\nfx_hex_codes=%d\ndim_muted=%d\nshow_meters=%d\ncursor_row_tint=%d\nwrap_cursor=%d\nstep_on_hex=%d\nnote_writes_ins=%d\n",
                 ins_colors, fx_hex_codes, dim_muted, show_meters, cursor_row_tint, wrap_cursor, step_on_hex, note_writes_ins);
    std::fprintf(f, "font_size_ui=%.1f\nfont_size_pattern=%.1f\nfont_tracking=%.2f\nwidget_gap=%.2f\nfont_ui=%s\nfont_mono=%s\nlast_dir=%s\n",
                 font_size_ui, font_size_pattern, font_tracking, widget_gap, font_ui, font_mono, last_dir);
    std::fclose(f);
    return true;
}

void Theme::apply_widget_colors() const {
    ImGuiStyle& st = ImGui::GetStyle();
    const float g = widget_gap < 0.5f ? 0.5f : widget_gap > 3.0f ? 3.0f : widget_gap;
    st.ItemSpacing = ImVec2(8 * g, 4 * g);
    st.ItemInnerSpacing = ImVec2(4 * g, 4 * g);
    st.FramePadding = ImVec2(4 * g, 3 * g);
    st.CellPadding = ImVec2(4 * g, 2 * g);
    st.WindowPadding = ImVec2(8 * g, 8 * g);
    st.IndentSpacing = 21 * g;
    const ImVec4 bg = colors[TC_PATTERN_BG];
    const ImVec4 accent = colors[TC_CHANNEL_HEADER_BG];
    const ImVec4 hot = colors[TC_CHANNEL_CURSOR];
    const ImVec4 text = colors[TC_CHANNEL_HEADER];
    auto mix = [](ImVec4 a, ImVec4 b, float t, float alpha = 1.0f) { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, alpha); };
    const ImVec4 white(1, 1, 1, 1), black(0, 0, 0, 1);
    const ImVec4 panel = mix(bg, white, 0.06f);
    const ImVec4 frame = mix(bg, white, 0.14f);
    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = mix(text, bg, 0.45f);
    c[ImGuiCol_WindowBg] = mix(bg, black, 0.15f);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = mix(bg, black, 0.05f, 0.96f);
    c[ImGuiCol_Border] = mix(accent, bg, 0.45f);
    c[ImGuiCol_FrameBg] = frame;
    c[ImGuiCol_FrameBgHovered] = mix(frame, accent, 0.5f);
    c[ImGuiCol_FrameBgActive] = mix(frame, hot, 0.6f);
    c[ImGuiCol_TitleBg] = panel;
    c[ImGuiCol_TitleBgActive] = mix(accent, bg, 0.35f);
    c[ImGuiCol_TitleBgCollapsed] = mix(bg, black, 0.2f, 0.8f);
    c[ImGuiCol_MenuBarBg] = mix(bg, black, 0.25f);
    c[ImGuiCol_ScrollbarBg] = mix(bg, black, 0.3f);
    c[ImGuiCol_ScrollbarGrab] = mix(accent, bg, 0.3f);
    c[ImGuiCol_ScrollbarGrabHovered] = accent;
    c[ImGuiCol_ScrollbarGrabActive] = hot;
    c[ImGuiCol_CheckMark] = hot;
    c[ImGuiCol_SliderGrab] = mix(accent, white, 0.15f);
    c[ImGuiCol_SliderGrabActive] = hot;
    c[ImGuiCol_Button] = mix(accent, bg, 0.25f);
    c[ImGuiCol_ButtonHovered] = accent;
    c[ImGuiCol_ButtonActive] = hot;
    c[ImGuiCol_Header] = mix(accent, bg, 0.4f);
    c[ImGuiCol_HeaderHovered] = mix(accent, bg, 0.15f);
    c[ImGuiCol_HeaderActive] = hot;
    c[ImGuiCol_Separator] = mix(accent, bg, 0.5f);
    c[ImGuiCol_SeparatorHovered] = accent;
    c[ImGuiCol_SeparatorActive] = hot;
    c[ImGuiCol_ResizeGrip] = mix(accent, bg, 0.5f, 0.6f);
    c[ImGuiCol_ResizeGripHovered] = accent;
    c[ImGuiCol_ResizeGripActive] = hot;
    c[ImGuiCol_Tab] = mix(accent, bg, 0.55f);
    c[ImGuiCol_TabHovered] = hot;
    c[ImGuiCol_TabSelected] = mix(accent, bg, 0.15f);
    c[ImGuiCol_TabSelectedOverline] = hot;
    c[ImGuiCol_TabDimmed] = mix(bg, black, 0.1f);
    c[ImGuiCol_TabDimmedSelected] = mix(accent, bg, 0.5f);
    c[ImGuiCol_TabDimmedSelectedOverline] = mix(accent, bg, 0.2f);
    c[ImGuiCol_DockingPreview] = mix(hot, white, 0.2f, 0.6f);
    c[ImGuiCol_DockingEmptyBg] = mix(bg, black, 0.3f);
    c[ImGuiCol_TableHeaderBg] = panel;
    c[ImGuiCol_TableBorderStrong] = mix(accent, bg, 0.4f);
    c[ImGuiCol_TableBorderLight] = mix(accent, bg, 0.7f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
    c[ImGuiCol_TextSelectedBg] = mix(hot, bg, 0.3f, 0.6f);
    c[ImGuiCol_NavCursor] = hot;
    c[ImGuiCol_PlotHistogram] = colors[TC_METER_HIGH];
    c[ImGuiCol_PlotHistogramHovered] = mix(colors[TC_METER_HIGH], white, 0.3f);
    c[ImGuiCol_PlotLines] = colors[TC_METER_LOW];
    c[ImGuiCol_PlotLinesHovered] = mix(colors[TC_METER_LOW], white, 0.3f);
    c[ImGuiCol_DragDropTarget] = hot;
}

Theme& theme() { return g_theme; }

namespace {
void draw_layout_tab(Theme& t) {
    ImGui::SeparatorText("Rows");
    ImGui::SetNextItemWidth(input_int_w(3)); ImGui::InputInt("highlight every", &t.row_hi1);
    ImGui::SetNextItemWidth(input_int_w(3)); ImGui::InputInt("strong highlight every", &t.row_hi2);
    if (t.row_hi1 < 1) t.row_hi1 = 1;
    if (t.row_hi2 < 1) t.row_hi2 = 1;
    ImGui::Checkbox("Hex row numbers", &t.hex_rows);
    ImGui::Checkbox("Tint the cursor's row", &t.cursor_row_tint);
    ImGui::SeparatorText("Follow");
    ImGui::SetNextItemWidth(em(14.7f));
    ImGui::Combo("scrolling", &t.follow_mode, "Smooth: pattern flows past a fixed line\0Jump: keep the playing row centred\0Page: scroll when the playing row reaches an edge\0");
    ImGui::SetNextItemWidth(em(10.7f));
    ImGui::SliderFloat("line position", &t.playhead_pos, 0.1f, 0.9f, "%.2f of the view");
    ImGui::SetNextItemWidth(em(10.7f));
    ImGui::SliderInt("latency compensation", &t.latency_ms, 0, 250, "%d ms");
    if (ImGui::IsItemHovered()) tooltip_spaced("The emulator runs ahead of the speakers by the audio buffering.\nRaise this if rows reach the line before you hear them, lower it if after.");

    ImGui::SeparatorText("Cells");
    ImGui::Checkbox("Colour instruments by number", &t.ins_colors);
    if (ImGui::IsItemHovered()) tooltip_spaced("Each instrument number gets its own hue so instrument changes stand out.");
    ImGui::Checkbox("Show effect opcodes as hex", &t.fx_hex_codes);
    if (ImGui::IsItemHovered()) tooltip_spaced("DA01 instead of Ins01. The hex is what you type into the effect column either way.");
    ImGui::Checkbox("Dim muted channels", &t.dim_muted);
    ImGui::Checkbox("Level meters in channel headers", &t.show_meters);

    ImGui::SeparatorText("Spacing");
    ImGui::SetNextItemWidth(em(10.7f)); ImGui::SliderFloat("widget gap", &t.widget_gap, 0.7f, 3.0f, "x%.2f");
    if (ImGui::IsItemHovered()) tooltip_spaced("Space between and inside widgets in every panel except the Player.\nWide pixel fonts read better around 1.5-2.");

    ImGui::SeparatorText("Editing");
    ImGui::SetNextItemWidth(input_int_w(3)); ImGui::InputInt("edit step", &t.edit_step);
    if (t.edit_step < 0) t.edit_step = 0;
    ImGui::Checkbox("Cursor wraps at pattern ends", &t.wrap_cursor);
    ImGui::Checkbox("Advance by edit step after a full hex entry", &t.step_on_hex);
    ImGui::Checkbox("Typing a note also writes the current instrument", &t.note_writes_ins);
    if (ImGui::IsItemHovered()) tooltip_spaced("Like Furnace: the instrument selected in the Instruments panel goes\nin with every note you type (when the track is on another one).");
}

void draw_colors_tab(Theme& t) {
    if (ImGui::Button("Default (red / pink / green)")) t.preset(0);
    same_line_if_fits("Blue tracker"); if (ImGui::Button("Blue tracker")) t.preset(4);
    same_line_if_fits("Furnace"); if (ImGui::Button("Furnace")) t.preset(3);
    same_line_if_fits("Classic blue"); if (ImGui::Button("Classic blue")) t.preset(1);
    same_line_if_fits("Amber"); if (ImGui::Button("Amber")) t.preset(2);
    same_line_if_fits("Enby"); if (ImGui::Button("Enby")) t.preset(5);
    child_begin("colors");
    for (int i = 0; i < TC_COUNT; ++i) {
        ImGui::PushID(i);
        ImGui::ColorEdit4("##c", &t.colors[i].x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        ImGui::SameLine();
        ImGui::TextUnformatted(t.names[i]);
        ImGui::PopID();
    }
    child_end();
}

bool font_choice(const char* label, char* path, size_t n, float& size) {
    const std::vector<FontChoice>& list = bundled_fonts();
    int current = 0;   // 0 = auto
    bool custom = path[0] != 0;
    for (size_t i = 0; i < list.size(); ++i) {
        std::string file = list[i].path.substr(list[i].path.find_last_of("/\\") + 1);
        if (file == path || list[i].path == path) { current = int(i) + 1; custom = false; }
    }
    if (custom) current = int(list.size()) + 1;
    const char* preview = current == 0 ? "Auto (system font)" : current <= int(list.size()) ? list[size_t(current - 1)].name.c_str() : "Custom path...";
    bool changed = false;
    ImGui::SetNextItemWidth(em(16.0f));
    if (ImGui::BeginCombo(label, preview)) {
        if (ImGui::Selectable("Auto (system font)", current == 0)) { path[0] = 0; changed = true; }
        for (size_t i = 0; i < list.size(); ++i) {
            const bool allowed = size >= list[i].min_size;
            char label_buf[128];
            if (allowed) std::snprintf(label_buf, sizeof label_buf, "%s", list[i].name.c_str());
            else std::snprintf(label_buf, sizeof label_buf, "%s  (needs %.0f px or more)", list[i].name.c_str(), list[i].min_size);
            if (ImGui::Selectable(label_buf, current == int(i) + 1, allowed ? 0 : ImGuiSelectableFlags_Disabled)) {
                std::string file = list[i].path.substr(list[i].path.find_last_of("/\\") + 1);
                std::snprintf(path, n, "%s", file.c_str());
                changed = true;
            }
        }
        if (ImGui::Selectable("Custom path...", current == int(list.size()) + 1) && !custom) { std::snprintf(path, n, "%s", "/"); changed = true; }
        ImGui::EndCombo();
    }
    if (current == int(list.size()) + 1) {
        same_line_if_fits(em(21.3f));
        ImGui::SetNextItemWidth(em(21.3f));
        ImGui::PushID(label);
        if (ImGui::InputText("##custom", path, n, ImGuiInputTextFlags_EnterReturnsTrue)) changed = true;
        ImGui::PopID();
    }
    return changed;
}

void draw_fonts_tab(Theme& t) {
    Fonts& F = fonts();
    text_wrapped("Auto picks a system font (DejaVu, Liberation, Noto, JetBrains Mono, ...) and falls back to ImGui's built-in one. "
                       "Fonts in assets/fonts are listed by name; the bundled pixel fonts look crispest at 16, 24 or 32 px.");
    auto base_name = [](const std::string& p) { return p.empty() ? std::string("(built-in)") : p.substr(p.find_last_of("/\\") + 1); };
    ImGui::TextDisabled("UI font in use: %s", base_name(F.ui_path).c_str());
    ImGui::TextDisabled("Pattern font in use: %s", base_name(F.mono_path).c_str());
    ImGui::Separator();
    bool changed = false;
    ImGui::SetNextItemWidth(em(10.7f)); ImGui::SliderFloat("UI font size", &t.font_size_ui, 10, 40, "%.0f px");
    changed |= font_choice("UI font", t.font_ui, sizeof t.font_ui, t.font_size_ui);
    ImGui::SetNextItemWidth(em(10.7f)); ImGui::SliderFloat("Pattern font size", &t.font_size_pattern, 8, 48, "%.0f px");
    changed |= font_choice("Pattern font", t.font_mono, sizeof t.font_mono, t.font_size_pattern);
    ImGui::SetNextItemWidth(em(10.7f));
    if (ImGui::SliderFloat("Letter spacing", &t.font_tracking, -3.0f, 3.0f, "%.1f px")) fonts_request_reload();
    if (ImGui::IsItemHovered()) tooltip_spaced("Extra space between glyphs. Pixel fonts like Mega Man X are drawn wide;\n-1 or -2 pulls the letters together and makes text easier to read from a distance.\nApplies to the UI font (the pattern font keeps its grid).");
    if (changed) fonts_request_reload();
    ImGui::TextDisabled("Ctrl + / Ctrl - / Ctrl 0 also zoom the pattern.");
    if (ImGui::Button("Reload fonts")) fonts_request_reload();
}

void draw_keyboard_tab() {
    if (ImGui::Button("Reset all to defaults")) { actions_reset_defaults(); g_capture_action = -1; }
    same_line_if_fits("Click a binding, press keys. Escape cancels, right-click clears.");
    ImGui::TextDisabled("Click a binding, press keys. Escape cancels, right-click clears.");
    if (g_capture_action >= 0) {
        Chord c;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) g_capture_action = -1;
        else if (chord_capture(c) != ImGuiKey_None) {
            action_binding(Action(g_capture_action)).c[g_capture_slot] = c;
            g_capture_action = -1;
        }
    }
    child_begin("keys");
    const char* group = "";
    if (ImGui::BeginTable("bindings", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 3);
        ImGui::TableSetupColumn("Primary", ImGuiTableColumnFlags_WidthStretch, 2);
        ImGui::TableSetupColumn("Secondary", ImGuiTableColumnFlags_WidthStretch, 2);
        ImGui::TableHeadersRow();
        for (int i = 0; i < A_COUNT; ++i) {
            const ActionDef& d = action_def(Action(i));
            if (std::strcmp(group, d.group)) {
                group = d.group;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", group);
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(d.name);
            for (int slot = 0; slot < 2; ++slot) {
                ImGui::TableNextColumn();
                ImGui::PushID(i * 2 + slot);
                char b[40];
                Chord& c = action_binding(Action(i)).c[slot];
                bool capturing = g_capture_action == i && g_capture_slot == slot;
                chord_format(c, b, sizeof b);
                if (capturing) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.3f, 0.2f, 1));
                if (ImGui::Button(capturing ? "press a key..." : *b ? b : "-", ImVec2(-1, 0))) { g_capture_action = i; g_capture_slot = slot; }
                if (capturing) ImGui::PopStyleColor();
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) { c = Chord{}; if (capturing) g_capture_action = -1; }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    child_end();
}

}

bool settings_capturing_key() { return g_capture_action >= 0; }

void draw_settings_window(bool* open) {
    ImGui::SetNextWindowSize(ImVec2(560, 620), ImGuiCond_FirstUseEver);
    if (!panel_begin("Settings", open)) { panel_end(); return; }
    Theme& t = g_theme;
    if (ImGui::Button("Save")) { t.save("boomspc_theme.ini"); actions_save("boomspc_keys.ini"); }
    same_line_if_fits("Load"); if (ImGui::Button("Load")) { t.load("boomspc_theme.ini"); actions_load("boomspc_keys.ini"); }
    same_line_if_fits("boomspc_theme.ini, boomspc_keys.ini (also saved on exit)"); ImGui::TextDisabled("boomspc_theme.ini, boomspc_keys.ini (also saved on exit)");
    if (ImGui::BeginTabBar("settings")) {
        if (ImGui::BeginTabItem("Layout"))   { draw_layout_tab(t); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Colours"))  { draw_colors_tab(t); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Fonts"))    { draw_fonts_tab(t); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Keyboard")) { draw_keyboard_tab(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    panel_end();
}

void draw_shortcuts_window(bool* open) {
    ImGui::SetNextWindowSize(ImVec2(480, 600), ImGuiCond_FirstUseEver);
    if (!panel_begin("Keyboard shortcuts", open)) { panel_end(); return; }
    text_wrapped("Note entry: Z..M and Q..P play the piano (two octaves), number row for sharps. "
                       "On the instrument / velocity / effect columns, 0-9 A-F type hex values. "
                       "Rebind everything in Settings > Keyboard.");
    ImGui::Separator();
    const char* group = "";
    if (ImGui::BeginTable("sc", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        for (int i = 0; i < A_COUNT; ++i) {
            const ActionDef& d = action_def(Action(i));
            const Binding& b = action_binding(Action(i));
            if (!b.c[0].bound() && !b.c[1].bound()) continue;
            if (std::strcmp(group, d.group)) {
                group = d.group;
                ImGui::TableNextRow(); ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", group);
            }
            char a[40], c[40];
            chord_format(b.c[0], a, sizeof a); chord_format(b.c[1], c, sizeof c);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(d.name);
            ImGui::TableNextColumn(); ImGui::Text("%s%s%s", a, *a && *c ? "  /  " : "", c);
        }
        ImGui::EndTable();
    }
    panel_end();
}
