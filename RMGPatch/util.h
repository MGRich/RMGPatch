#pragma once
#include <iostream>
#include <sstream>
#include <vector>
#include <filesystem>
#include <sys/stat.h>
#include <sys/types.h>
#include <memory>

//#define _DEBUG
#if defined(_DEBUG) && defined(_WIN32)
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#define DEBUG_NEW new(_NORMAL_BLOCK, __FILE__, __LINE__)
#define new DEBUG_NEW
#endif

typedef int64_t int64;
typedef unsigned char byte;
typedef std::vector<byte> charvec;
typedef uint32_t uint;

inline int64 MAX(int64 a, int64 b) { return((a) > (b) ? a : b); }
inline int64 MIN(int64 a, int64 b) { return((a) < (b) ? a : b); }

static int memory = 2;

//QUICK UTIL FUNCS
inline int64 len(charvec vector) { return vector.size(); }
inline int getbytes(int64 in) {
    int r = 0;
    do {
        in >>= 8;
        r++;
    } while (in);
    return r;
}
void writeint(charvec& vector, int64 in, int size) {
    for (int i = 0; i < size; i++) {
        vector.push_back(in & 0xFF);
        in >>= 8;
    }
}
uint64_t vectoint(charvec vector) {
    uint64_t ret = 0;
    for (int i = vector.size(); i-- > 0;) {
        ret <<= 8;
        ret |= vector[i];
    };
    return ret;
}
std::vector<std::string> split(std::string s, char delim) {
    using namespace std;
    string sbuf;
    vector<string> r;
    istringstream str(s);
    while (getline(str, sbuf, delim)) {
        r.push_back(sbuf);
    }
    return r;
}
std::string join(std::vector<std::string> sv, std::string joined) {
    using namespace std;
    if (!sv.size()) return "";
    string r;
    for (const string& s : sv) {
        r.append(s);
        r.append(joined);
    }
    r.erase(r.end() - joined.size(), r.end());
    return r;
}

//FILE MANAGEMENT
charvec read(byte* mem, int len, int64& pos, int64 max) {
    int count = (int)MIN(max - pos, len);
    byte* buf = new byte[count];
    if (memory) memcpy(buf, (void*)(mem + pos), count);
    else (*(std::ifstream*)mem).read((char*)buf, count);
    pos += count;
    charvec ret(buf, buf + count);
    delete[] buf;
    return ret;
}
int64 seek(byte* mem, int64 pos, int whence, int64 max, int64& posref) {
    if (!whence) posref = MIN(max, pos);
    else if (whence == 1) posref = MAX(MIN(max, posref + pos), 0);
    else if (whence == 2) posref = MAX(max - pos, 0);
    if (!memory) (*(std::ifstream*)mem).seekg(pos, whence);
    return posref;
}
charvec readvec(charvec& vector, int len, int64& pos) {
    int count = (int)MIN(vector.size() - pos, len);
    charvec res(vector.begin() + pos, vector.begin() + pos + count);
    pos += count;
    return res;
}
inline uint64_t readintvec(charvec& vector, int len, int64& pos) {
    return vectoint(readvec(vector, len, pos));
}

//DIRECTORIES
struct Dir {
    inline Dir() {
        initialized = false;
        parent = nullptr; //shut UP
    }
    inline Dir(std::string n, Dir* p = new Dir()) :
        name{ n }, parent{ p } { initialized = true; };

    std::string path() {
        Dir* cdir = this;
        std::string res;
        while (cdir->initialized) {
            res.insert(0, cdir->name);
            res.insert(0, "/");
            cdir = cdir->parent;
        }
        res.erase(0, 1);
        return res;
    }
    std::vector<std::string> walklist(bool includedir = false) {
        std::vector<std::string> ret;
        for (auto itr = this->children.begin(); itr != this->children.end(); itr++) {
            auto inpth = itr->get();
            if (includedir || (!includedir && !inpth->isdir)) ret.push_back(inpth->path());
            if (inpth->isdir) {
                std::vector<std::string> recur = inpth->walklist();
                ret.insert(ret.end(), recur.begin(), recur.end());
            }
        }
        return ret;
    }
    Dir* find(std::string path, bool create) {
        if (path == "") {
            return this;
        }
        auto spl = split(path, '/');
        for (auto itr = this->children.begin(); itr != this->children.end(); itr++) {
            auto p = (itr->get());
            if (p->name == spl[0]) {
                spl.erase(spl.begin());
                return p->find(join(spl, "/"), create);
            }
        }
        if (!create) {
            return new Dir();
        }
        Dir* pb = new Dir(spl[0], this);
        this->children.push_back(std::unique_ptr<Dir>(pb));
        spl.erase(spl.begin());
        return this->children.back()->find(join(spl, "/"), create);
    }

    std::string name;
    Dir* parent;
    std::vector<std::unique_ptr<Dir>> children;
    byte initialized = false;
    int64 filesize = -1;
    bool isdir = true;
};

struct DirIterator {
    inline DirIterator(Dir* dir) : current{ dir } {}
    Dir* next(bool enter = true) {
        if (enter && canenter) {
            current = current->children[locs.back()].get();
            locs.push_back(-1);
            deep++;
        }
        if (++locs.back() >= current->children.size()) {
            current = current->parent;
            locs.pop_back();
            if (--deep < 0) {
                delete this;
                return nullptr;
            }
            canenter = false;
            return this->next();
        }
        canenter = current->children[locs.back()]->isdir;
        return current->children[locs.back()].get();
    }
    Dir* current;
    bool canenter = false;
    std::vector<int> locs = { -1 };
    int deep = 0;
};

std::unique_ptr<Dir> filesystodir(std::string& rootstr) {
    using namespace std;
    using namespace std::filesystem;
    string stbuf;
    unique_ptr<Dir> rootdir = unique_ptr<Dir>(new Dir());
#ifdef _WIN32 
    string rcl;
    istringstream rootstm(rootstr);
    while (getline(rootstm, stbuf, '\\')) {
        rcl.append(stbuf);
        rcl.append("/");
    }
    //rcl.pop_back();
    rootstr = rcl;
#else
    //maybe smth here
#endif
    if (rootstr.back() != '/') rootstr.append("/");
    for (recursive_directory_iterator i(rootstr), end; i != end; i++) {
        auto path = i->path();
        string fullpathstr = path.string();
#ifdef _WIN32 
        string filtered;
        istringstream fpstm(fullpathstr);
        while (getline(fpstm, stbuf, '\\')) {
            filtered.append(stbuf);
            filtered.append("/");
        }
        filtered.pop_back();
        fullpathstr = filtered;
#else
        //do smth here for linux? hope not
#endif
        string pathstr = fullpathstr;
        if (!pathstr.rfind(rootstr, 0)) {
            pathstr.erase(0, rootstr.size());
        }
        Dir* d = rootdir->find(pathstr, true);
        if (!is_directory(i->path())) {
            struct stat f;
            if (stat(path.string().c_str(), &f)) {
                cout << "unable to open stat for file " << pathstr << endl;
                return std::unique_ptr<Dir>();
            }
            d->filesize = f.st_size;
            d->isdir = false;
        }
        //cout << d->path() << endl;
    }
    rootdir->filesize = 0; //use as return code of sorts
    return rootdir;
}

void readdheader(charvec& vector, int64& pos, byte& fl, Dir* parent) {
    unsigned short count = readintvec(vector, 2, pos);
    for (uint i = 0; i < count; i++) {
        byte strc = readintvec(vector, 1, pos);
        charvec dirv = readvec(vector, strc & ~0x80, pos);
        std::string dirs(dirv.begin(), dirv.end());
        parent->children.push_back(std::unique_ptr<Dir>(new Dir(dirs, parent)));
        if (!(strc & 0x80))
            readdheader(vector, pos, fl, parent->children.back().get());
        else {
            parent->children.back()->isdir = false;
            byte typ = readintvec(vector, 1, pos);
            if (typ != 2) {
                parent->children.back()->filesize = readintvec(vector, fl, pos);
                if ((typ & 0xF) == 1) {
                    parent->children.back()->initialized = 2 + ((typ & 0xF0) >> 4);
                }
            }
        }
    }
    //return std::unique_ptr<Dir>(parent);
}