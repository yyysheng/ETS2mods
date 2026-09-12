#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

struct Node { std::uintptr_t address{}; int depth{}; std::string path; };

static DWORD game_pid() {
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snap==INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W e{sizeof(e)}; DWORD result=0;
    if(Process32FirstW(snap,&e)) do {
        if(_wcsicmp(e.szExeFile,L"eurotrucks2.exe")==0){result=e.th32ProcessID;break;}
    } while(Process32NextW(snap,&e));
    CloseHandle(snap); return result;
}

static bool readable(HANDLE p,std::uintptr_t a) {
    MEMORY_BASIC_INFORMATION m{};
    if(!VirtualQueryEx(p,reinterpret_cast<void*>(a),&m,sizeof(m))) return false;
    if(m.State!=MEM_COMMIT || (m.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return false;
    const DWORD prot=m.Protect&0xff;
    return prot==PAGE_READONLY||prot==PAGE_READWRITE||prot==PAGE_WRITECOPY||
           prot==PAGE_EXECUTE_READ||prot==PAGE_EXECUTE_READWRITE||prot==PAGE_EXECUTE_WRITECOPY;
}

int wmain(int argc,wchar_t **argv) {
    if(argc<6){std::cerr<<"usage: HookMemoryProbe <hookZ> <rootHex> <rootHex> <rootHex> <rootHex>\n";return 2;}
    const float wanted=std::stof(argv[1]);
    const DWORD pid=game_pid(); if(!pid){std::cerr<<"game not found\n";return 3;}
    HANDLE process=OpenProcess(PROCESS_VM_READ|PROCESS_QUERY_INFORMATION,FALSE,pid);
    if(!process){std::cerr<<"OpenProcess failed\n";return 4;}
    std::deque<Node> queue;
    for(int i=2;i<argc;++i) queue.push_back({static_cast<std::uintptr_t>(std::stoull(argv[i],nullptr,16)),0,"root"+std::to_string(i-2)});
    std::unordered_set<std::uintptr_t> seen;
    std::size_t nodes=0,scalar_hits=0,vector_hits=0;
    constexpr std::size_t block_size=0x1800;
    while(!queue.empty()&&nodes<5000) {
        Node node=std::move(queue.front()); queue.pop_front();
        const auto page=node.address&~static_cast<std::uintptr_t>(0xfff);
        if(!seen.insert(page).second||!readable(process,node.address)) continue;
        std::vector<std::uint8_t> bytes(block_size); SIZE_T got{};
        if(!ReadProcessMemory(process,reinterpret_cast<void*>(node.address),bytes.data(),bytes.size(),&got)||got<16) continue;
        bytes.resize(got); ++nodes;
        for(std::size_t off=0;off+sizeof(float)<=bytes.size();off+=4) {
            float z{}; std::memcpy(&z,bytes.data()+off,sizeof(z));
            if(!std::isfinite(z)||std::abs(z-wanted)>0.0025f) continue;
            ++scalar_hits;
            bool vector=false; float x{},y{};
            if(off>=8){std::memcpy(&x,bytes.data()+off-8,4);std::memcpy(&y,bytes.data()+off-4,4);vector=std::abs(x)<0.10f&&std::abs(y-1.0f)<0.20f;}
            if(vector) ++vector_hits;
            std::cout<<(vector?"VECTOR":"SCALAR")<<" address=0x"<<std::hex<<(node.address+off)<<std::dec
                     <<" base=0x"<<std::hex<<node.address<<std::dec<<" offset=0x"<<std::hex<<off<<std::dec
                     <<" depth="<<node.depth<<" path="<<node.path
                     <<" xyz=("<<x<<','<<y<<','<<z<<")\n";
        }
        if(node.depth>=3) continue;
        const std::size_t pointer_limit=std::min<std::size_t>(bytes.size(),0x1000);
        for(std::size_t off=0;off+sizeof(std::uintptr_t)<=pointer_limit;off+=8) {
            std::uintptr_t child{}; std::memcpy(&child,bytes.data()+off,sizeof(child));
            if(child<0x10000000000ULL||child>0x7fffffffffffULL||!readable(process,child)) continue;
            queue.push_back({child,node.depth+1,node.path+"+0x"+[&]{char b[24];sprintf_s(b,"%zx",off);return std::string(b);}()});
        }
    }
    std::cout<<"SUMMARY nodes="<<nodes<<" scalarHits="<<scalar_hits<<" vectorHits="<<vector_hits<<"\n";
    CloseHandle(process); return vector_hits?0:5;
}
