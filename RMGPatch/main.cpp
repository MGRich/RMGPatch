#include <fstream>
#include <map>
#include <algorithm>
#include <iterator>
#include <random>
#include <array>
#include "util.h"

#define CRCPP_USE_CPP11
#ifdef _WIN32
#define ZLIB_WINAPI 
#endif
#include "dependencies/CRC.h"
#include "dependencies/zlib/zlib.h"
#include "dependencies/lzma/LzmaLib.h"

static bool verbose = false;
static bool docompare = false;
#ifdef _DEBUG
static int chsize = 0x8000;
#else
static int chsize = 0x800;
#endif
static int lensize = 0x200;

static int bytecount[3] = {0, 0, 0};
static bool include[3] = {1, 1, 1};

void showhelp() {
    using namespace std;
    cout <<
        "usage: pt <command> [<args>] [--memory=X] [--include(a/r/d)=y]" << endl <<
        "commands:" << endl <<
        "      create         - creates a patch out of 2 files or directories" << endl <<
        "        <original> <edited> <patchfile> [--crccmp=n] [--chsize=0x800] [--lensize=0x200]" << endl <<
        "      apply          - applies a patch to a file or directory" << endl <<
        "        <original> <patchfile> [output]" << endl <<
        "        output will not be used for directories" << endl <<
        "switches:" << endl <<
        "    --memory         - store files in memory. defaults to n for creation and y for applying" << endl <<
    //  "    --verbose  - print detailed/debug info. defaults to n" << endl <<
        "    --chsize         - the \"chunk size.\" how much to read each time. the higher the faster," << endl <<
        "        but its best to keep it moderate. only accepts integer values (no hex.) defaults to 0x800 (2048)" << endl <<
        "    --lensize        - the comparison size. higher means more accuracy and less errorprone but slower speed." << endl <<
        "        only accepts integer values (no hex.) defaults to 0x200 (512)" << endl <<
        "    --crccmp         - compare files with CRC-32 to check if they are the same instead of attempting to make" << endl <<
        "        a patch to see if they're the same. this is slower and more memory intensive. defaults to n" << endl <<
        "    --include(a/r/d) - includea, includer, included; a for additions, r for removals, and d for changed files" << endl <<
        "        this can be used for both creation and applying directory patches. all default to y" << endl;
}

charvec createpatch(std::ifstream ogfile, std::ifstream edfile, bool header, uint crc = 0);
charvec applypatch(std::ifstream ogfile, std::ifstream ptfile, bool header, int& code);

int main(int argc, char* argv[]) {
    #if defined(_WIN32) && defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    #endif
    if (argc <= 2) {
        showhelp();
        return 0;    
    }
    bool create = !strcmp(argv[1], "create") || !strcmp(argv[1], "c");
    if (!create && !(!strcmp(argv[1], "apply") || !strcmp(argv[1], "patch") || !strcmp(argv[1], "a"))) {
        printf("invalid command\n");
        showhelp();
        return 1;
    }
    struct stat f;
    if (stat(argv[2], &f) || !(f.st_mode & (S_IFDIR | S_IFREG))) {
        printf("invalid file or folder\n");
        return 2;
    }
    bool isfolder = (f.st_mode & S_IFDIR);
    char buf[10];
    for (int i = (create ? 4 : (isfolder ? 3 : 4)); i < argc; i++) {
        if (argv[i][0] == '-') {
            if (isfolder) {
                if (!strncmp("--include", argv[i], 9)) {
                    byte target = 3;
                    if (argv[i][9] == 'd') target = 0;
                    else if (argv[i][9] == 'a') target = 1;
                    else if (argv[i][9] == 'r') target = 2;
                    else {
                        std::cout << "invalid switch " << argv[i] << std::endl;
                        continue;
                    }
                    include[target] = argv[i][10] == 'y';
                    continue;   
                }
            }
            if (!strncmp("--memory", argv[i], 8)) {
                memory = argv[i][9] == 'y';
            }
            else if (!strncmp("--verbose", argv[i], 9)) {
                verbose = argv[i][10] == 'y';
            }
            else if (create) {
                if (!strncmp("--chsize", argv[i], 8)) {
                    strcpy(buf, argv[i] + 9);
                    chsize = 0;
                    for (uint j = 0; j < strlen(argv[i] + 9); j++) {
                        chsize *= 10;
                        chsize += (buf[j] - 0x30);
                    }
                }
                else if (!strncmp("--lensize", argv[i], 9)) {
                    strcpy(buf, argv[i] + 10);
                    lensize = 0;
                    for (uint j = 0; j < strlen(argv[i] + 10); j++) {
                        lensize *= 10;
                        lensize += (buf[j] - 0x30);
                    }
                }
                else if (!strncmp("--crccmp", argv[i], 8)) {
                    verbose = argv[i][10] == 'y';
                }
            }
            else std::cout << "invalid switch " << argv[i] << std::endl;
        }
    }
    using namespace std;
    if (create) {
        if (memory == 2) memory = false;
        if (isfolder) {
            if (argc < 4) {
                showhelp();
                return 1;
            }
            struct stat f;
            if (stat(argv[3], &f) || !(f.st_mode & S_IFDIR)) {
                printf("invalid folder %s\n", argv[3]);
                return 2;
            }
            unique_ptr<Dir> rootdir[2];
            string rootstr[2];
            for (int i = 0; i < 2; i++) {
                rootstr[i] = argv[i+2];
                rootdir[i] = filesystodir(rootstr[i]);
            }
            //both of our dirs are ready now we need to find the differences
            vector<string> walked[2];
            for (int i = 0; i < 2; i++) {
                walked[i] = rootdir[i]->walklist();
                sort(walked[i].begin(), walked[i].end());
            }
            vector<string> onlyin[2];
            vector<string> shared;
            int bytec = 0;
            if (include[2]) set_difference(walked[0].begin(), walked[0].end(), walked[1].begin(), walked[1].end(), back_inserter(onlyin[0]));
            if (include[1]) set_difference(walked[1].begin(), walked[1].end(), walked[0].begin(), walked[0].end(), back_inserter(onlyin[1]));
            if (include[0]) set_intersection(walked[0].begin(), walked[0].end(), walked[1].begin(), walked[1].end(), back_inserter(shared));
            charvec outbuf, dirhead; //AND SO WE BEGIN
            unique_ptr<Dir> dwritten = unique_ptr<Dir>(new Dir());
            vector<array<int64, 2>> inb;
            for (const string& str : shared) {
                for (int i = 0; i < 3; i++) bytecount[i] = 0;
                cout << str << endl;
                string fpath[2];
                for (int i = 0; i < 2; i++) fpath[i] = rootstr[i] + str;
                ifstream og(fpath[0], ios::binary | ios::in);
                ifstream ed(fpath[1], ios::binary | ios::in);
                int ogfs = rootdir[0]->find(str, false)->filesize;
                int edfs = rootdir[1]->find(str, false)->filesize;
                char* ogb = new char[ogfs];
                og.read(ogb, ogfs);
                uint c = CRC::Calculate(ogb, ogfs, CRC::CRC_32());
                delete[] ogb;
                og.close();
                if (docompare && ogfs == edfs) {
                    char* edb = new char[ogfs];
                    ed.read(edb, ogfs);
                    uint edc = CRC::Calculate(edb, ogfs, CRC::CRC_32());
                    delete[] edb;
                    if (c == edc) {
                        cout << " identical" << endl;
                        continue;
                    }
                }
                ed.close();
                charvec r = createpatch(ifstream(fpath[0], ios::binary | ios::in), ifstream(fpath[1], ios::binary | ios::in), false, c);
                if (!r.size()) {
                    cout << " identical" << endl;
                    continue;
                }
                Dir* dirout = dwritten->find(str, true);
                dirout->filesize = outbuf.size(); //use filesize as position
                dirout->isdir = false;
                inb.push_back({(int64)outbuf.size(), (int64)r.size()});
                bytec = MAX(getbytes(r.size()), bytec);
                outbuf.insert(outbuf.end(), r.begin(), r.end());
                r.~vector();
            }
            for (const string& str : onlyin[0]) { //deletions
                cout << str << endl << " deleted" << endl;
                Dir* dirout = dwritten->find(str, true);
                dirout->filesize = -1; //-1 to signal deletion
                dirout->isdir = false;
            }
            for (const string& str : onlyin[1])   //calc bytesize for additions
                bytec = MAX(getbytes(rootdir[1]->find(str, false)->filesize), bytec);
            for (const string& str : onlyin[1]) { //additions
                cout << str << endl << " added" << endl;
                Dir* dirout = dwritten->find(str, true);
                dirout->filesize = outbuf.size(); //use loc
                string fpath = rootstr[1] + str;
                ifstream added(fpath, ios::binary | ios::in);
                int64 fs = rootdir[1]->find(str, false)->filesize;
                char* buf = new char[fs];
                added.read(buf, fs);
                charvec r(buf, buf + fs);
                char used = 0;
                Bytef* out = new Bytef[compressBound(fs)];
                uLongf outsize = compressBound(fs);
                compress2(out, &outsize, (Bytef*)buf, fs, 9);
                if (outsize < fs) {
                    r.~vector();
                    r = charvec(out, out+outsize);
                    used = 1;
                }
                delete[] out;
                out = new Bytef[fs * 2];
                size_t lzmasize = fs * 2;
                char* props = new char[5];
                size_t propssize = 5;
                int rcode = LzmaCompress(out, &lzmasize, (unsigned char*)buf, fs, (unsigned char*)props, &propssize, 9, 0, -1, -1, -1, -1, -1);
                if (!rcode && lzmasize < r.size()) {
                    r.~vector();
                    r = charvec(out, out + lzmasize);
                    used = 2;
                }
                delete[] out;
                delete[] buf;
                dirout->initialized = 2 + used; //store used thing in here
                dirout->isdir = false;
                if (used) writeint(outbuf, fs, bytec);
                writeint(outbuf, r.size(), bytec);
                outbuf.insert(outbuf.end(), r.begin(), r.end());
                r.~vector();
                if (used == 2) {
                    charvec propvec(props, props + 5);
                    outbuf.insert(outbuf.end(), propvec.begin(), propvec.end());
                    propvec.~vector();
                }
            }
            //now we have to write the header
            shared.~vector();
            for (int i = 0; i < 2; i++) {
                walked[i].~vector();
                onlyin[i].~vector();
            }
            DirIterator* itr = new DirIterator(dwritten.get());
            writeint(dirhead, dwritten->children.size(), 2);
            while (Dir* x = itr->next()) {
                cout << x->path() << endl;
                dirhead.push_back((Byte)x->name.size() | (0x80 * !x->isdir));
                dirhead.insert(dirhead.end(), x->name.begin(), x->name.end());
                if (!x->isdir) {
                    char typ = 0;
                    if (x->initialized > true) typ = 1;
                    else if (x->filesize < 0)  typ = 2;
                    if (typ == 1) {
                        typ |= (((x->initialized & 0b111) - 2) << 4);
                    }
                    dirhead.push_back(typ);
                    if (typ != 2) {
                        writeint(dirhead, x->filesize, getbytes(outbuf.size()));
                    }
                }
                else writeint(dirhead, x->children.size(), 2);
            }
            ofstream out(argv[4], ios::binary | ios::out);
            charvec h({ 'X', 'X', 'X', 0x80 });
            out.write((char*)h.data(), 4);
            h[0] = getbytes(outbuf.size()) | (bytec << 4);
            out.write((char*)h.data(), 1);
            out.write((char*)dirhead.data(), dirhead.size());
            for (const array<int64, 2> & x : inb) {
                charvec ref(outbuf.begin(), outbuf.begin() + x[0]);
                writeint(ref, x[1], bytec);
                outbuf.erase(outbuf.begin(), outbuf.begin() + x[0]);
                out.write((char*)ref.data(), ref.size());
                ref.~vector();
            }
            out.write((char*)outbuf.data(), outbuf.size());
            out.close();
            return 0;
        }
        else {
            struct stat f;
            stat(argv[2], &f);
            int64 ogfs = f.st_size;
            stat(argv[3], &f);
            int64 edfs = f.st_size;
            ifstream og(argv[2], ios::binary | ios::in);
            ifstream ed(argv[3], ios::binary | ios::in);
            char* ogb = new char[ogfs];
            og.read(ogb, ogfs);
            uint c = CRC::Calculate(ogb, ogfs, CRC::CRC_32());
            delete[] ogb;
            og.close();
            if (docompare && ogfs == edfs) {
                char* edb = new char[ogfs];
                ed.read(edb, ogfs);
                uint edc = CRC::Calculate(edb, ogfs, CRC::CRC_32());
                delete[] edb;
                if (c == edc) {
                    cout << "files are the same" << endl;
                    return 0;
                }
            }
            ed.close();
            charvec r = createpatch(ifstream(argv[2], ios::binary | ios::in), ifstream(argv[3], ios::binary | ios::in), true, c);
            if (!r.size()) {
                cout << "files are the same" << endl;
                return 0;
            }
            ofstream out(argv[4], ios::binary | ios::out);
            out.write((char*)r.data(), r.size());
            r.~vector();
            out.close();
            return 0;
        }
    }
    else {
        if (memory == 2) memory = true;
        if (isfolder) {
            namespace fs = std::filesystem;
            charvec h({ 'X', 'X', 'X', 0x80 });
            ifstream pt(argv[3], ios::binary | ios::in);
            pt.seekg(0, 2);
            int64 ptp = 0, ptl = pt.tellg();
            pt.seekg(0);
            char* ptb = new char[ptl];
            pt.read(ptb, ptl);
            charvec ptvec(ptb, ptb + ptl);
            delete[] ptb;
            pt.close();
            if (readvec(ptvec, 4, ptp) != h) {
                cout << "invalid header" << endl;
                return 3;
            }
            //LOL i cant be assed to type "unsigned char" since byte is ambigous here so use zlib's Byte
            Byte bc = readintvec(ptvec, 1, ptp);
            Byte ac = (bc & 0xF0) >> 4;
            bc &= 0xF;
            Dir* rootdir = new Dir();
            readdheader(ptvec, ptp, bc, rootdir);
            int64 addend = ptp;
            int fails = 0;
            DirIterator* iter = new DirIterator(rootdir);
            while (Dir* x = iter->next()) {
                if (x->isdir) continue;
                string wholepdir = argv[2];
                wholepdir += "/" + x->parent->path();
                string wholedir = wholepdir + "/" + x->name;
                if (x->filesize == -1 && include[2]) {
                    if (!fs::remove(wholedir)) {
                        cout << x->path() << " already did not exist" << endl;
                        fails++;
                    }
                    else cout << "removed " << x->path() << endl;
                }
                else if (x->initialized > 1 && include[1]) {
                    if (fs::exists(wholedir)) cout << x->path() << " exists, will be overwritten" << endl;
                    Byte typ = x->initialized - 2;
                    ptp = x->filesize + addend;
                    charvec dat;
                    int64 uncmp = readintvec(ptvec, ac, ptp);
                    if (!typ) {
                        dat = readvec(ptvec, uncmp, ptp);
                    }
                    else {
                        dat = readvec(ptvec, readintvec(ptvec, ac, ptp), ptp);
                        Bytef* out = new Bytef[uncmp];
                        if (typ == 1) {
                            uLongf ucmp = uncmp;
                            uncompress(out, &ucmp, (Bytef*)dat.data(), dat.size());
                            dat.~vector();
                        }
                        else {
                            size_t ucmp = uncmp;
                            charvec props = readvec(ptvec, 5, ptp);
                            size_t size = dat.size();
                            LzmaUncompress(out, &ucmp, (Byte*)dat.data(), &size, (Byte*)props.data(), 5);
                            dat.~vector();
                        }
                        dat = charvec(out, out + uncmp);
                    }
                    ofstream out(wholedir + "/" + x->name, ios::binary | ios::out);
                    out.write((char*)dat.data(), uncmp);
                    dat.~vector();
                    out.close();
                    cout << x->path() << " added";
                }
                else if (include[0]) {
                    if (!fs::exists(wholedir)) {
                        cout << x->path() << " does not exist, will be skipped" << endl;
                        fails++;
                        continue;
                    }
                    srand(time(nullptr));
                    int r = rand();
                    string fpath = "tmp" + to_string(r);
                    ofstream pt(fpath, ios::binary | ios::out);
                    ptp = x->filesize + addend;
                    int64 rl = readintvec(ptvec, ac, ptp);
                    charvec snippet(ptvec.begin() + ptp, ptvec.begin() + ptp + rl);
                    pt.write((char*)snippet.data(), snippet.size());
                    snippet.~vector();
                    pt.close();
                    int code;
                    charvec result = applypatch(ifstream(wholedir, ios::binary | ios::in), ifstream(fpath, ios::binary | ios::in), false, code);
                    fs::remove(fpath);
                    if (code) {
                        cout << "patch for " << x->path() << " was unsuccessful, skipping" << endl;
                        result.~vector();
                        fails++;
                        continue;
                    }
                    pt = ofstream(wholedir, ios::binary | ios::in);
                    pt.write((char*)result.data(), result.size());
                    pt.close();
                    cout << "applied patch to " << x->path() << endl;
                    result.~vector();
                }
            }
            cout << "patching finished with " << fails << " failures/skips";
            return fails;
        }
        else {
            int code;
            charvec result = applypatch(ifstream(argv[2], ios::binary | ios::in), ifstream(argv[3], ios::binary | ios::in), true, code);
            ofstream out(argv[4], ios::binary | ios::out);
            out.write((char*)result.data(), result.size());
            out.close();
            return 0;
        }
    }
}

charvec createpatch(std::ifstream ogfile, std::ifstream edfile, bool header, uint crcv) {
    charvec outbuf;
    std::vector<std::vector<int64>> inbuf; 
    byte *og, *ed;
    int64 ogpos = 0, edpos = 0, ogmax = 0, edmax = 0;
    short count = 0;
    ogfile.seekg(0, 2);
    edfile.seekg(0, 2);
    ogmax = ogfile.tellg();
    edmax = edfile.tellg();
    ogfile.seekg(0, 0);
    edfile.seekg(0, 0);
    if (memory) {
        og = new byte[ogmax];
        ed = new byte[edmax];
        ogfile.read((char*)og, ogmax);
        edfile.read((char*)ed, edmax);
        ogfile.close();
        edfile.close(); //we no longer need these so dont waste space
    } 
    else {
        og = (byte*)&ogfile;
        ed = (byte*)&edfile;
    }
    uint32_t crcval = crcv;
    bytecount[0] = getbytes(ogmax);
    auto publish = [&](charvec& data, int len, bool add, int loc) {
        int bytes = getbytes(len);
        if (len >> (bytes * 8 - 1)) bytes++;
        bytecount[1] = MAX(bytes, bytecount[1]);
        inbuf.push_back({len, (int)outbuf.size(), add, 1});
        writeint(outbuf, loc, bytecount[0]);
        int64 cmpsize = data.size();
        charvec written = data;
        if (add) {
            bytecount[2] = MAX(getbytes(data.size()), bytecount[2]);
            byte used = 0;
            byte* out = new byte[compressBound(cmpsize)];
            uLongf outsize = compressBound(cmpsize);
            compress2(out, &outsize, data.data(), cmpsize, 9);
            if (outsize < cmpsize) {
                written.~vector();
                written = charvec(out, out + outsize);
                used = 1;
            }
            delete[] out;
            out = new byte[cmpsize * 2];
            size_t lzmasize = cmpsize * 2;
            byte* props = new byte[5];
            size_t propssize = 5; 
            int rcode = LzmaCompress(out, &lzmasize, data.data(), cmpsize, props, &propssize, 9, 0, -1, -1, -1, -1, -1);
            if (!rcode && lzmasize < cmpsize) {
                written.~vector();
                written = charvec(out, out + lzmasize);
                used = 2;
            }
            delete[] out;
            data.~vector();
            writeint(outbuf, used, 1);
            if (used) inbuf.push_back({cmpsize, (int64)outbuf.size(), 0, 2});
            inbuf.push_back({(int64)written.size(), (int64)outbuf.size(), 0, 2});
            outbuf.insert(outbuf.end(), written.begin(), written.end());
            written.~vector();
            if (used == 2) {
                charvec propvec(props, props + 5);
                outbuf.insert(outbuf.end(), propvec.begin(), propvec.end());
                propvec.~vector();
            }
            delete[] props;
        }
        data.~vector();
        using namespace std;
        cout << "PUB #" << ++count << " AT " << hex << loc << " OGLEN " << hex << len << " NEWLEN " << hex << cmpsize << (add ? " REPLACEMENT" : " DELETION") << endl;
    };
    while (read(ed, 1, edpos, edmax).size()) {
        seek(ed, -1, 1, edmax, edpos);
        charvec readog = read(og, chsize, ogpos, ogmax);
        charvec readed = read(ed, chsize, edpos, edmax);
        if (readog == readed) continue;
        seek(og, -len(readog), 1, ogmax, ogpos);
        seek(ed, -len(readed), 1, edmax, edpos);
        readed.~vector();
        readog.~vector();
        while (true) {
            readog = read(og, 1, ogpos, ogmax);
            readed = read(ed, 1, edpos, edmax);
            if (readog != readed) break;
        }
        seek(og, -len(readog), 1, ogmax, ogpos);
        seek(ed, -len(readed), 1, edmax, edpos);
        readed.~vector();
        readog.~vector();
        int64 loc = ogpos, found;
        using namespace std;
        cout << "FOUND OG " << hex << loc << " ED " << hex << edpos << endl;
        charvec dat;
        bool first = true;
        charvec full = read(og, ogmax, ogpos, ogmax);
        seek(og, loc, 0, ogmax, ogpos);
        bool go = len(full) > lensize;
        if (!go) {
            dat = read(ed, edmax, edpos, edmax);
            found = loc;
            first = false;
        }
        while (go) {
            charvec cmp = read(ed, lensize, edpos, edmax);
            if (len(cmp) < lensize) {
                found = ogmax;
                seek(og, 0, 2, ogmax, ogpos);
                seek(ed, -len(cmp), 1, edmax, edpos);
                readed = read(ed, edmax, edpos, edmax);
                dat.insert(dat.end(), readed.begin(), readed.end());
                readed.~vector();
                first = false;
                break;
            }
            auto it = search(full.begin(), full.end(), cmp.begin(), cmp.end());
            if (it != full.end()) {
                found = (it - full.begin()) + loc;
                seek(ed, -(lensize + chsize), 1, edmax, edpos);
                dat.resize(dat.size() - chsize);
                for (int i = 0; i < chsize - 1; i++) {
                    readed = read(ed, 1, edpos, edmax);
                    dat.push_back(readed[0]);
                    readed.~vector();
                    charvec dcmp = read(ed, lensize, edpos, edmax);
                    auto itd = search(full.begin(), it, dcmp.begin(), dcmp.end());
                    seek(ed, -lensize, 1, edmax, edpos);
                    if (itd != it) {
                        found = (itd - full.begin()) + loc;
                        cmp = dcmp;
                        dcmp.~vector();
                        break;
                    }
                }
                seek(ed, lensize, 1, edmax, edpos);
                seek(og, found + lensize, 0, ogmax, ogpos);
                break;
            }
            first = false;
            seek(ed, -len(cmp), 1, edmax, edpos);
            readed = read(ed, chsize, edpos, edmax);
            dat.insert(dat.end(), readed.begin(), readed.end());
            cmp.~vector();
            readed.~vector();
        }
        full.~vector();
        publish(dat, found - loc, !first, loc);
    }
    if (read(og, 1, ogpos, ogmax).size()) {
        seek(og, -1, 1, ogmax, ogpos);
        int64 loc = ogpos;
        charvec empty = charvec();
        publish(empty, len(read(og, ogmax, ogpos, ogmax)), false, loc);
    }
    if (memory) {
        delete[] og;
        delete[] ed;
    }
    if (!count) return charvec();
    charvec final;
    if (header) {
        final.push_back('X'); final.push_back('X'); final.push_back('X'); final.push_back(0);
    }
    writeint(final, crcval, 4); //crc
    writeint(final, (((bytecount[2]) << 4 ) | bytecount[1]), 1);
    writeint(final, count, 2);
    int last = 0;
    std::vector<int> safelist;
    for (uint i = 0; i < inbuf.size(); i++) {
        safelist.push_back(inbuf[i][1] - last);
        last = inbuf[i][1];
    }
    last = 0;
    for (uint i = 0; i < safelist.size(); i++) {
        final.insert(final.end(), outbuf.begin() + last, outbuf.begin() + last + safelist[i]);
        last += safelist[i];
        std::vector<int64> ininfo = inbuf[0];
        inbuf.erase(inbuf.begin());
        int64 ntowrite = ininfo[0];
        if (ininfo[2]) ntowrite |= ((int64)1 << ((bytecount[ininfo[3]] * 8) - 1));
        writeint(final, ntowrite, bytecount[ininfo[3]]);
    }
    final.insert(final.end(), outbuf.begin() + last, outbuf.end());
    return final;
}

charvec applypatch(std::ifstream ogfile, std::ifstream ptfile, bool header, int& code) {
    charvec outbuf;
    byte *og, *pt;
    int64 ogpos = 0, ptpos = 0, ogmax = 0, ptmax = 0;
    short count = 0;
    ogfile.seekg(0, 2);
    ptfile.seekg(0, 2);
    ogmax = ogfile.tellg();
    ptmax = ptfile.tellg();
    ogfile.seekg(0, 0);
    ptfile.seekg(0, 0);
    if (memory) {
        og = new byte[ogmax];
        pt = new byte[ptmax];
        ogfile.read((char*)og, ogmax);
        ptfile.read((char*)pt, ptmax);
        ogfile.close();
        ptfile.close(); //we no longer nept these so dont waste space
    } 
    else {
        og = (byte*)&ogfile;
        pt = (byte*)&ptfile;
    }
    bytecount[0] = getbytes(ogmax);
    uint32_t crcval;    
    charvec readall;
    if (memory)
        readall = charvec(og, og + ogmax);
    else {
        readall = read(og, ogmax, ogpos, ogmax);
        seek(og, 0, 0, ogmax, ogpos);
    }
    crcval = CRC::Calculate(readall.data(), ogmax, CRC::CRC_32());
    readall.~vector();
    auto readint = [&](int size) {
        return vectoint(read(pt, size, ptpos, ptmax));
    };
    if (header) {
        charvec h = read(pt, 4, ptpos, ptmax);
        if (h != charvec({'X', 'X', 'X', 0})) {
            printf("header doesn't match\n");
            code = 1;
            return charvec();
        }
    }
    if (crcval != readint(4)) {
        printf("crc value does not match\n");
        if (memory) {
            delete[] og;
            delete[] pt;
        }
        code = 2;
        return charvec();
    }
    byte hb = readint(1);
    std::vector<std::map<std::string, int64>> inftbl;
    bytecount[1] = hb & 0xF;
    bytecount[2] = hb >> 4;
    int64 mask = ((int64)1 << ((bytecount[1] * 8) - 1));
    unsigned short c = readint(2);
    for (unsigned short i = 0; i < c; i++) {
        std::map<std::string, int64> tmp;
        int64 len = readint(bytecount[1]);
        tmp.emplace("len", (len & ~mask));
        tmp.emplace("pos", readint(bytecount[0]));
        tmp.emplace("add", len & mask);
        if (len & mask) {
            tmp.emplace("typ", readint(1));
            if (tmp["typ"]) tmp.emplace("ulen", readint(bytecount[2]));
            tmp.emplace("clen", readint(bytecount[2]));
            tmp.emplace("off", ptpos);
            seek(pt, tmp["clen"] + (tmp["typ"] == 2 ? 5 : 0), 1, ptmax, ptpos);
        }
        inftbl.push_back(tmp);
    }
    std::vector<int64> safelist;
    int64 last = 0;
    for (uint i = 0; i < inftbl.size(); i++) {
        safelist.push_back(inftbl[i]["pos"] - last);
        last = inftbl[i]["pos"] + inftbl[i]["len"];
    }
    for (uint i = 0; i < safelist.size(); i++) {
        charvec ogread = read(og, safelist[i], ogpos, ogmax);
        charvec dat;
        outbuf.insert(outbuf.end(), ogread.begin(), ogread.end());
        std::map<std::string, int64> info = inftbl[0];
        inftbl.erase(inftbl.begin());
        seek(og, info["len"], 1, ogmax, ogpos);
        if (info["add"]) {
            seek(pt, info["off"], 0, ptmax, ptpos);
            dat = read(pt, info["clen"], ptpos, ptmax);
            if (info["typ"]) {
                byte* out = new byte[info["ulen"]];
                if (info["typ"] == 1) {
                    uLongf ulen = info["ulen"];
                    uncompress(out, &ulen, dat.data(), dat.size());
                }
                else {
                    charvec props = read(pt, 5, ptpos, ptmax);
                    size_t ulen = info["ulen"];
                    SizeT size = dat.size();
                    LzmaUncompress(out, &ulen, dat.data(), &size, props.data(), 5);
                }
                dat = charvec(out, out + info["ulen"]);
                delete[] out;
            }
            outbuf.insert(outbuf.end(), dat.begin(), dat.end());
        }
        using namespace std;
        cout << (info["add"] ? "REPLACE " : "REMOVED ") << "AT POS " << hex << info["pos"] << " OGLEN " << info["len"];
        if (info["add"]) cout << " EDLEN " << hex << dat.size();
        cout << endl;
    }
    charvec rest = read(og, ogmax, ogpos, ogmax);
    if (memory) {
        delete[] pt;
        delete[] og;
    }
    outbuf.insert(outbuf.end(), rest.begin(), rest.end());
    code = 0;
    return outbuf;
    
}