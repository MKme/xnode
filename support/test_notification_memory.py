"""Exercise production notification storage with deterministic allocation failures."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
compiler = shutil.which('clang++') or shutil.which('g++')
if not compiler:
    raise SystemExit('Existing C++ compiler required')
source = (root / 'src/utils/msg_chain.cpp').read_text()
# Keep every production function unchanged; replace only platform/allocator includes.
source = source[source.index('msg_chain_t * msg_chain_add_msg'):]
test = r'''
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <set>
#include <cstdio>
#include "utils/msg_chain.h"
static int fail_at = -1, calls = 0;
static std::set<void*> allocations;
static void* test_calloc(size_t n, size_t size) {
    if (calls++ == fail_at) return nullptr;
    void* p = calloc(n,size); if(p) allocations.insert(p); return p;
}
static void test_free(void* p) {
    if (p) assert(allocations.erase(p) == 1);
    free(p);
}
#define CALLOC test_calloc
#define free test_free
#define log_e(...) ((void)0)
#define log_i(...) do { if(false) printf(__VA_ARGS__); } while(0)
#include "actual_msg_chain.inc"
int main() {
    // Every allocation in first-message and subsequent-message creation can fail.
    for(int failure=0; failure<3; ++failure) {
        calls=0; fail_at=failure;
        assert(msg_chain_add_msg(nullptr,"incoming") == nullptr);
        assert(allocations.empty());
    }
    for(int failure=0; failure<2; ++failure) {
        fail_at=-1;
        auto* chain=msg_chain_add_msg(nullptr,"preserve me");
        auto* saved=chain;
        calls=0; fail_at=failure;
        chain=msg_chain_add_msg(chain,"incoming");
        assert(chain==saved && msg_chain_get_entrys(chain)==1);
        assert(strcmp(msg_chain_get_msg_entry(chain,0),"preserve me")==0);
        assert(msg_chain_add_msg(chain,nullptr)==chain);
        chain=msg_chain_delete(chain);
        assert(chain==nullptr && allocations.empty());
    }
    fail_at=-1;
    auto* chain=msg_chain_add_msg(nullptr,"first");
    for(int i=0;i<1000;++i) {
        char text[32]; snprintf(text,sizeof(text),"message %d",i);
        chain=msg_chain_add_msg(chain,text);
        while(msg_chain_get_entrys(chain)>32) assert(msg_chain_delete_msg_entry(chain,0));
        assert(strcmp(msg_chain_get_msg_entry(chain,msg_chain_get_entrys(chain)-1),text)==0);
        assert(allocations.size()<=65);
    }
    assert(strcmp(msg_chain_get_msg_entry(chain,0),"message 968")==0);
    assert(msg_chain_delete(chain)==nullptr && allocations.empty());
    puts("PASS: all allocation failures preserve inbox, no leaks/double frees, 1000-message bounded retention");
}
'''
with tempfile.TemporaryDirectory(prefix='xnode-notification-memory-', dir=root / '.pio') as scratch:
    temp = Path(scratch)
    (temp / 'actual_msg_chain.inc').write_text(source)
    (temp / 'test.cpp').write_text(test)
    exe = temp / 'test.exe'
    subprocess.run([compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(root / 'src'), str(temp / 'test.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=20)
