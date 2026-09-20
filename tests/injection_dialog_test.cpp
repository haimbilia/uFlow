#include "injection_dialog.hpp"
#include <cassert>
#include <iostream>
int main(){
    InjectionDialog d;assert(!d.Open());
    d.Ask("Bonk");assert(d.state==InjectionDialog::State::Confirm&&!d.yes&&!d.Consume());
    d.Input(0,true,false);assert(!d.Open()); // safe default: no
    d.Ask("Bonk");d.Input(1,false,false);assert(d.yes&&!d.Consume());
    d.Input(0,true,false);assert(d.state==InjectionDialog::State::Working&&d.Consume()&&!d.Consume());
    d.Progress("Copying",20,100);assert(d.bytes==20&&d.total==100);
    d.Input(0,false,true);assert(d.cancel&&d.Open());
    d.Finish("Cancelled");assert(d.state==InjectionDialog::State::Message&&!d.Consume());
    d.Touch(640,480);assert(!d.Open());
    d.Ask("Bonk");d.Touch(700,400);assert(!d.Consume());
    d.Touch(700,480);assert(d.Consume());d.Finish("");assert(!d.Open());
    d.Ask("Bonk");d.Touch(420,480);assert(!d.Open());
    d.Ask("Bonk");d.Input(0,false,true);assert(!d.Open());
    std::cout<<"PASS: injection confirmation, safe No default, Yes one-shot, progress, cancel, message and touch bounds\n";
}
