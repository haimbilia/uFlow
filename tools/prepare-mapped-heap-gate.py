"""Apply the pinned, reviewed mapped-heap serialization change without other edits."""
import argparse, subprocess
from pathlib import Path
REVISION = "b849fecf421f9f7ce3056d8d08581ef0bb0a4d91"
OPERATIONS = [
("void *", "MEMAllocFromExpHeapEx", "MEMHeapHandle heap, uint32_t size, int alignment", "heap, size, alignment"),
("void", "MEMFreeToExpHeap", "MEMHeapHandle heap, void *block", "heap, block"),
("MEMExpHeapMode", "MEMSetAllocModeForExpHeap", "MEMHeapHandle heap, MEMExpHeapMode mode", "heap, mode"),
("MEMExpHeapMode", "MEMGetAllocModeForExpHeap", "MEMHeapHandle heap", "heap"),
("uint32_t", "MEMAdjustExpHeap", "MEMHeapHandle heap", "heap"),
("uint32_t", "MEMResizeForMBlockExpHeap", "MEMHeapHandle heap, void *block, uint32_t size", "heap, block, size"),
("uint32_t", "MEMGetTotalFreeSizeForExpHeap", "MEMHeapHandle heap", "heap"),
("uint32_t", "MEMGetAllocatableSizeForExpHeapEx", "MEMHeapHandle heap, int alignment", "heap, alignment"),
("uint16_t", "MEMSetGroupIDForExpHeap", "MEMHeapHandle heap, uint16_t id", "heap, id"),
("uint16_t", "MEMGetGroupIDForExpHeap", "MEMHeapHandle heap", "heap"),
("void", "MEMVisitAllocatedForExpHeap", "MEMHeapHandle heap, MEMExpHeapBlockVisitor callback, void *context", "heap, callback, context"),
("BOOL", "MEMCheckExpHeap", "MEMHeapHandle heap, MEMExpHeapCheckFlags mode", "heap, mode"),
]
def prepare(root, apply):
    revision=subprocess.check_output(["git","-C",str(root),"rev-parse","HEAD"],text=True).strip()
    if revision!=REVISION: raise ValueError("Unexpected MemoryMapping source revision")
    def original(name):return subprocess.check_output(["git","-C",str(root),"show",f"{REVISION}:{name}"],text=True)
    replacements=original("source/function_replacements.cpp")
    hooks="#include <coreinit/memexpheap.h>\n#include \"mapped_heap_gate_native.hpp\"\n"
    for ret,name,args,call in OPERATIONS:
        hooks+=f"DECL_FUNCTION({ret}, {name}, {args}) {{\n    MappedHeapGuard<MappedHeapMutex> guard(MemoryMappingGateForHeap(heap));\n    return real_{name}({call});\n}}\n"
    replacements=replacements.replace("// clang-format off\nfunction_replacement",hooks+"\n// clang-format off\nfunction_replacement")
    start='function_replacement_data_t function_replacements[] __attribute__((section(".data"))) = {\n'
    replacements=replacements.replace(start,start+''.join(f"        REPLACE_FUNCTION({n}, LIBRARY_COREINIT, {n}),\n" for _,n,_,_ in OPERATIONS))
    main=original("source/main.cpp").replace('#include "version.h"','#include "version.h"\n#include "mapped_heap_gate_native.hpp"')
    main=main.replace('#define VERSION "v0.2.7"','#define VERSION "v0.2.7-uflow-heap-gate1"')
    main=main.replace('    MemoryMapping_CreateHeaps();','    MemoryMappingGateInit();\n    MemoryMapping_CreateHeaps();')
    mapping=original("source/memory_mapping.cpp").replace('#include "memory_mapping.h"','#include "memory_mapping.h"\n#include "mapped_heap_gate_native.hpp"')
    mapping=mapping.replace('bool CheckMemExpHeap(MEMExpHeap *heap) {','bool CheckMemExpHeap(MEMExpHeap *heap) {\n    MappedHeapGuard<MappedHeapMutex> guard(MemoryMappingGateForHeap(&heap->header));')
    native='#pragma once\n#include "mapped_heap_gate.hpp"\n#include <coreinit/memheap.h>\n#include <coreinit/mutex.h>\nstruct MappedHeapMutex {\n    OSMutex mutex;\n    void lock() { OSLockMutex(&mutex); }\n    void unlock() { OSUnlockMutex(&mutex); }\n};\nvoid MemoryMappingGateInit();\nMappedHeapMutex *MemoryMappingGateForHeap(MEMHeapHandle heap);\n'
    cpp='#include "mapped_heap_gate_native.hpp"\n#include "memory_mapping.h"\nstatic MappedHeapMutex gate;\nvoid MemoryMappingGateInit() { OSInitMutex(&gate.mutex); }\nMappedHeapMutex *MemoryMappingGateForHeap(MEMHeapHandle heap) {\n    const auto address=reinterpret_cast<uintptr_t>(heap);\n    for(unsigned i=0;mem_mapping[i].physical_addresses;++i)\n        if(address>=mem_mapping[i].effective_start_address && address<mem_mapping[i].effective_end_address)\n            return &gate;\n    return nullptr;\n}\n'
    changes={"source/function_replacements.cpp":replacements,"source/main.cpp":main,"source/memory_mapping.cpp":mapping,
        "source/mapped_heap_gate_native.hpp":native,"source/mapped_heap_gate.cpp":cpp,
        "source/mapped_heap_gate.hpp":Path(__file__).with_name("mapped-heap-gate.hpp").read_text()}
    previous=dict(changes)
    changes["source/main.cpp"]=main.replace("uflow-heap-gate1", "uflow-heap-mutex2")
    # Establish synchronization at heap creation, never change live heap flags.
    changes["source/memory_mapping.cpp"]=mapping.replace(
        "MEMCreateExpHeapEx(address, size, MEM_HEAP_FLAG_USE_LOCK)",
        "MEMCreateExpHeapEx(address, size, 0) /* All mapped-heap operations use the recursive gate. */")
    prior_mutex=dict(changes)
    extra = """DECL_FUNCTION(MEMHeapHandle, MEMCreateExpHeapEx, MEMHeapHandle heap, uint32_t size, uint16_t flags) {
    MappedHeapGuard<MappedHeapMutex> guard(MemoryMappingGateForHeap(heap));
    return real_MEMCreateExpHeapEx(heap, size, flags);
}
DECL_FUNCTION(void *, MEMDestroyExpHeap, MEMHeapHandle heap) {
    MappedHeapGuard<MappedHeapMutex> guard(MemoryMappingGateForHeap(heap));
    return real_MEMDestroyExpHeap(heap);
}
"""
    changes["source/function_replacements.cpp"]=replacements.replace(
        "// clang-format off\nfunction_replacement", extra+"\n// clang-format off\nfunction_replacement").replace(
        start, start+"        REPLACE_FUNCTION(MEMCreateExpHeapEx, LIBRARY_COREINIT, MEMCreateExpHeapEx),\n        REPLACE_FUNCTION(MEMDestroyExpHeap, LIBRARY_COREINIT, MEMDestroyExpHeap),\n")
    changes["source/memory_mapping.cpp"]=changes["source/memory_mapping.cpp"].replace(
        "MEMHeapHandle MemoryMapping_MEMFindContainHeap(void *block) {",
        "MEMHeapHandle MemoryMapping_MEMFindContainHeap(void *block) {\n    MappedHeapGuard<MappedHeapMutex> guard(MemoryMappingGateForHeap(static_cast<MEMHeapHandle>(block)));")
    for name,expected in changes.items():
        path=root/name
        actual=path.read_text() if path.exists() else None
        if actual==expected:continue
        if name in ("source/function_replacements.cpp","source/main.cpp","source/memory_mapping.cpp"):
            if actual not in (original(name),previous[name],prior_mutex[name]):raise ValueError("Unrelated source edits: "+name)
        elif actual is not None:raise ValueError("Unexpected existing file: "+name)
        if not apply:raise ValueError("Patch missing: "+name)
        path.write_text(expected,newline="\n")
    print("PASS: mapped-heap gate source verified (14 heap entry points and lookup, mapped heaps use one recursive mutex)")
if __name__=="__main__":
    p=argparse.ArgumentParser();p.add_argument("source",type=Path);p.add_argument("--apply",action="store_true")
    a=p.parse_args();prepare(a.source,a.apply)
