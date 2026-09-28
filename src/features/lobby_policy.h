// SPDX-License-Identifier: MIT
#pragma once

#include <string>

namespace lobby_policy {

struct Locks {
    bool teams = false;
    bool slots = false;
};

Locks Current();

void SetLocks(const Locks& locks);

void ApplyCreateChoiceOnce();

Locks CreateChoice();
void SetCreateChoice(const Locks& locks);

bool Password(std::string* out);

struct LockEvent {
    bool teams;
    bool locked;
};
bool NextLockEvent(LockEvent* out);

}  // namespace lobby_policy
