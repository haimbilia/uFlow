#include "controller_pairing.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main(){
    using P=Controllers::PairingSession;P pairing;
    pairing.Begin(100);
    // Accepted but discovery has not started: never end the session early.
    for(unsigned now:{100u,350u,1100u,5100u})assert(pairing.Poll(now)==P::Result::Waiting);
    pairing.Event(1);assert(pairing.Poll(1100)==P::Result::Waiting);
    pairing.Event(0);assert(pairing.Poll(1500)==P::Result::Waiting);
    pairing.Event(1);assert(pairing.Poll(1600)==P::Result::Finished);
    pairing.Cancel();pairing.Event(1);assert(pairing.Poll(90000)==P::Result::Waiting);
    pairing.Begin(2000);assert(pairing.Poll(61999)==P::Result::Waiting);assert(pairing.Poll(62000)==P::Result::TimedOut);
    pairing.Begin(0xfffffff0u);assert(pairing.Poll(40)==P::Result::Waiting);assert(pairing.Poll(uint32_t(0xfffffff0u+60000u))==P::Result::TimedOut);
    pairing.Cancel();pairing.Event(0);pairing.Event(1);assert(pairing.Poll(500)==P::Result::Waiting);
    puts("PASS delayed discovery, ordered completion, stale finish, cancel, retry, timeout and clock wrap");
}
