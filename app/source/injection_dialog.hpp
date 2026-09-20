#pragma once
#include <algorithm>
#include <cstdint>
#include <string>

struct InjectionDialog {
    enum class State { Closed, Confirm, Working, Message };
    State state=State::Closed;
    std::string title,phase,message;
    bool yes=false,ready=false,cancel=false;
    std::uint64_t bytes=0,total=0;
    bool Open() const {return state!=State::Closed;}
    void Reset(){*this={};}
    void Ask(const std::string &name){Reset();title=name;state=State::Confirm;}
    void Start(const std::string &name){Reset();title=name;state=State::Working;phase="Preparing...";}
    void Input(int direction,bool accept,bool back){
        if(state==State::Confirm){
            if(back){Reset();return;}
            if(direction)yes=direction>0;
            if(accept){if(yes){state=State::Working;phase="Preparing...";ready=true;}else Reset();}
        }else if(state==State::Working){if(back)cancel=true;}
        else if(state==State::Message&&(accept||back))Reset();
    }
    void Touch(int x,int y){
        if(y<456||y>=508)return;
        if(state==State::Confirm){
            if(x>=380&&x<620){yes=false;Input(0,true,false);}
            else if(x>=660&&x<900){yes=true;Input(0,true,false);}
        }else if(x>=500&&x<780)Input(0,state==State::Message,state==State::Working);
    }
    bool Consume(){const bool result=ready;ready=false;return result;}
    void Progress(const std::string &value,std::uint64_t done,std::uint64_t size){
        phase=value;total=size;bytes=size?std::min(done,size):done;
    }
    void Finish(const std::string &error){
        if(error.empty()){Reset();return;}
        state=State::Message;ready=false;message=error;
    }
};
