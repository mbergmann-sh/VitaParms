// VitaParms – akustische Hinweise (Qt Multimedia, sonst Systemton)
// SPDX-License-Identifier: MIT
#pragma once

namespace Sounds {
enum class Kind { Reminder, Done, CountStart, CountStop };
void play(Kind k);
void play(Kind k, bool loadOnly);
void preload(Kind k);   // Klang vorab laden, damit er sofort ertönt
bool available();     // mit Qt Multimedia übersetzt?
}
