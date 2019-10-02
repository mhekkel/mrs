//   Copyright Maarten L. Hekkelman, Radboud University 2012.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           http://www.boost.org/LICENSE_1_0.txt)

#include "M6Lib.h"

#include <iostream>
#include <cmath>
#include <atomic>

#include <boost/filesystem/operations.hpp>

#include "M6Parser.h"
#include "M6Error.h"
#include "M6Config.h"

using namespace std;
namespace fs = boost::filesystem;

// --------------------------------------------------------------------
// Perl based parser implementation

#if defined(_MSC_VER)
#include <Windows.h>
#endif
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#undef isnan
#undef INT64_C    //
#undef bind
#undef bool

// ------------------------------------------------------------------

extern "C"
{
void xs_init(pTHX);
void boot_DynaLoader(pTHX_ CV* cv);
void boot_Win32CORE(pTHX_ CV* cv);
}

// ------------------------------------------------------------------

struct M6ParserImpl
{
                        M6ParserImpl(const string& inScriptName);
                        ~M6ParserImpl();

    void                Parse(M6InputDocument* inDoc,
                            const string& inFileName, const string& inDbHeader);
    string                GetVersion(const string& inSourceConfig);
    void                GetIndexNames(vector<pair<string,string>>& outIndexNames);
    void                ToFasta(const string& inDoc, const string& inDb, const string& inID,
                            const string& inTitle, string& outFasta);

    // implemented callbacks
    void                IndexText(const string& inIndex, const char* inText, size_t inLength)
                            { mDocument->Index(inIndex, eM6TextData, false, inText, inLength); }
    void                IndexString(const string& inIndex, const char* inText, size_t inLength, bool inUnique)
                            { mDocument->Index(inIndex, eM6StringData, inUnique, inText, inLength); }
    void                IndexDate(const string& inIndex, const char* inText, size_t inLength)
                            { mDocument->Index(inIndex, eM6DateData, false, inText, inLength); }
    void                IndexNumber(const string& inIndex, const char* inText, size_t inLength)
                            { mDocument->Index(inIndex, eM6NumberData, false, inText, inLength); }
    void                IndexFloat(const string& inIndex, const char* inText, size_t inLength)
                            { mDocument->Index(inIndex, eM6FloatData, false, inText, inLength); }

    void                AddLink(const string& inDatabank, const string& inValue)
                            { mDocument->AddLink(inDatabank, inValue); }
    void                SetAttribute(const string& inField, const char* inValue, size_t inLength)
                            { mDocument->SetAttribute(inField, inValue, inLength); }
    void                SetDocument(const char* inText, size_t inLength)
                            { mDocument->SetText(string(inText, inLength)); }

    // Perl interface routines
    static const char*    kM6ScriptType;
    static const char*    kM6ScriptObjectName;

    static M6ParserImpl*
                        GetObject(SV* inScalar);

    HV*                    GetHash()                { return mParserHash; }

    string                operator[](const char* inEntry);
    static string        GetString(SV* inScalar);

    string                mName;
    PerlInterpreter*    mPerl;
    SV*                    mParser;
    HV*                    mParserHash;
    static M6ParserImpl* sConstructingImp;
    static boost::mutex    sInitMutex;

    M6InputDocument*    mDocument;
};

const char* M6ParserImpl::kM6ScriptType = "M6::Script";
const char* M6ParserImpl::kM6ScriptObjectName = "M6::Script::Object";
M6ParserImpl* M6ParserImpl::sConstructingImp = nullptr;
boost::mutex M6ParserImpl::sInitMutex;

M6ParserImpl::M6ParserImpl(const string& inScriptName)
    : mName(inScriptName)
    , mPerl(nullptr)
    , mParser(nullptr)
    , mParserHash(nullptr)
{
    boost::mutex::scoped_lock lock(sInitMutex);

    static bool sInited = false;
    if (not sInited)
    {
        int argc = 0;
        const char* env[] = { nullptr };
        const char* argv[] = { nullptr };

        PERL_SYS_INIT3(&argc, (char***)&argv, (char***)&env);
        sInited = true;
    }

    fs::path scriptdir = M6Config::GetDirectory("parser");
    if (not fs::exists(scriptdir))
    {
        if (fs::exists("./parsers"))
            scriptdir = fs::path("./parsers");
        else
            THROW(("scriptdir not found or incorrectly specified (%s)", scriptdir.c_str()));
    }

    mPerl = perl_alloc();
    if (mPerl == nullptr)
        THROW(("error allocating perl interpreter"));

    perl_construct(mPerl);

    PL_origalen = 1;

    PERL_SET_CONTEXT(mPerl);

    fs::path baseParser = scriptdir / "M6Script.pm";

    if (not fs::exists(baseParser))
        THROW(("The M6Script.pm script could not be found in %s", scriptdir.c_str()));

    string baseParserPath = baseParser.string();
    const char* embedding[] = { "", baseParserPath.c_str() };

    mParserHash = newHV();

    // init the parser hash
//    (void)hv_store_ent(mParserHash,
//        newSVpv("db", 2),
//        newSVpv(inDatabank.c_str(), inDatabank.length()), 0);
//
//    string path = inRawDir.string();
//    (void)hv_store_ent(mParserHash,
//        newSVpv("raw_dir", 7),
//        newSVpv(path.c_str(), path.length()), 0);
//    (void)hv_store_ent(mParserHash,
//        newSVpv("script_dir", 7),
//        newSVpv(path.c_str(), path.length()), 0);

    int err = perl_parse(mPerl, xs_init, 2, const_cast<char**>(embedding), nullptr);
    SV* errgv = GvSV(PL_errgv);

    if (err != 0)
    {
        string errmsg = "no perl error available";

        if (SvPOK(errgv))
            errmsg = string(SvPVX(errgv), SvCUR(errgv));

        THROW(("Error parsing M6Script.pm module: %s", errmsg.c_str()));
    }

    dSP;
    ENTER;
    SAVETMPS;
    PUSHMARK(SP);

    string path = scriptdir.string();
    XPUSHs(sv_2mortal(newSVpv(path.c_str(), path.length())));
    XPUSHs(sv_2mortal(newSVpv(inScriptName.c_str(), inScriptName.length())));

    PUTBACK;

    int n;
    static boost::mutex sGlobalGuard;
    {
        boost::mutex::scoped_lock lock(sGlobalGuard);

        sConstructingImp = this;
        n = call_pv("M6::load_script", G_SCALAR | G_EVAL);
        sConstructingImp = nullptr;
    }

    SPAGAIN;

    if (n != 1 or SvTRUE(ERRSV))
    {
        string errmsg(SvPVX(ERRSV), SvCUR(ERRSV));
        THROW(("Perl error creating script object: %s", errmsg.c_str()));
    }

    mParser = newRV(POPs);

    PUTBACK;
    FREETMPS;
    LEAVE;
}

M6ParserImpl::~M6ParserImpl()
{
    boost::mutex::scoped_lock lock(sInitMutex);

    PERL_SET_CONTEXT(mPerl);

    PL_perl_destruct_level = 0;
    perl_destruct(mPerl);
    perl_free(mPerl);
}

void M6ParserImpl::Parse(M6InputDocument* inDocument,
    const string& inFileName, const string& inDbHeader)
{
    mDocument = inDocument;

    const string& text = mDocument->Peek();

    PERL_SET_CONTEXT(mPerl);

    dSP;
    ENTER;
    SAVETMPS;
    PUSHMARK(SP);

    XPUSHs(SvRV(mParser));
    XPUSHs(sv_2mortal(newSVpv(text.c_str(), text.length())));
    XPUSHs(sv_2mortal(newSVpv(inFileName.c_str(), inFileName.length())));
    if (not inDbHeader.empty())
        XPUSHs(sv_2mortal(newSVpv(inDbHeader.c_str(), inDbHeader.length())));

    PUTBACK;

    call_method("parse", G_SCALAR | G_EVAL);

    SPAGAIN;

    string errmsg;
    if (SvTRUE(ERRSV))
        errmsg.assign(SvPVX(ERRSV), SvCUR(ERRSV));

    PUTBACK;
    FREETMPS;
    LEAVE;

    mDocument = nullptr;

    if (not errmsg.empty())
    {
        cerr << endl
             << "Error parsing document: " << endl
             << errmsg << endl;
        THROW(("Error parsing document: %s", errmsg.c_str()));
    }
}

string M6ParserImpl::GetVersion(const string& inSourceConfig)
{
    PERL_SET_CONTEXT(mPerl);

    dSP;
    ENTER;
    SAVETMPS;
    PUSHMARK(SP);

    XPUSHs(SvRV(mParser));
    XPUSHs(sv_2mortal(newSVpv(inSourceConfig.c_str(), inSourceConfig.length())));

    PUTBACK;

    int n = call_method("version", G_EVAL);

    SPAGAIN;

    string result, errmsg;

    if (SvTRUE(ERRSV))
        errmsg = string(SvPVX(ERRSV), SvCUR(ERRSV));
    else if (n == 1 and SvPOK(*SP))
        result = SvPVX(POPs);

    PUTBACK;
    FREETMPS;
    LEAVE;

    if (errmsg.length())
        THROW(("Error calling version: %s", errmsg.c_str()));

    return result;
}

void M6ParserImpl::GetIndexNames(vector<pair<string,string>>& outIndexNames)
{
    PERL_SET_CONTEXT(mPerl);

    SV** sv = hv_fetch(mParserHash, "indices", 7, 0);
    if (sv != nullptr)
    {
        HV* hv = nullptr;

        if (SvTYPE(*sv) == SVt_PVHV)
            hv = (HV*)*sv;
        else if (SvROK(*sv))
        {
            SV* rv = SvRV(*sv);
            if (SvTYPE(rv) == SVt_PVHV)
                hv = (HV*)rv;
        }

        if (hv != nullptr)
        {
            uint32 n = hv_iterinit(hv);

            while (n-- > 0)
            {
                STRLEN len;

                HE* he = hv_iternext(hv);

                if (he == nullptr)
                    break;

                string id = HePV(he, len);

                SV* v = HeVAL(he);
                if (v != nullptr)
                {
                    string desc = SvPV(v, len);
                    outIndexNames.push_back(make_pair(id, desc));
                }
            }
        }
    }
}

void M6ParserImpl::ToFasta(const string& inDocument, const string& inDb, const string& inID,
    const string& inTitle, string& outFasta)
{
    outFasta.clear();

    PERL_SET_CONTEXT(mPerl);

    dSP;
    ENTER;
    SAVETMPS;
    PUSHMARK(SP);

    XPUSHs(SvRV(mParser));
    XPUSHs(sv_2mortal(newSVpv(inDocument.c_str(), inDocument.length())));
    XPUSHs(sv_2mortal(newSVpv(inDb.c_str(), inDb.length())));
    XPUSHs(sv_2mortal(newSVpv(inID.c_str(), inID.length())));
    XPUSHs(sv_2mortal(newSVpv(inTitle.c_str(), inTitle.length())));

    PUTBACK;

    int n = call_method("to_fasta", G_SCALAR | G_EVAL);

    SPAGAIN;

    string errmsg;

    if (SvTRUE(ERRSV))
        errmsg = string(SvPVX(ERRSV), SvCUR(ERRSV));
    else if (n == 1 and SvPOK(*SP))
        outFasta = SvPVX(POPs);

    PUTBACK;
    FREETMPS;
    LEAVE;

    if (errmsg.length())
        THROW(("Error calling to_fasta: %s", errmsg.c_str()));
//
//    if (n != 1 or outFasta.empty())
//        THROW(("to_fasta method of parser script should return one string"));
}

M6ParserImpl* M6ParserImpl::GetObject(
    SV*                    inScalar)
{
    M6ParserImpl* result = nullptr;

    if (SvGMAGICAL(inScalar))
        mg_get(inScalar);

    if (sv_isobject(inScalar))
    {
        SV* tsv = SvRV(inScalar);

        IV tmp = 0;

        if (SvTYPE(tsv) == SVt_PVHV)
        {
            if (SvMAGICAL(tsv))
            {
                MAGIC* mg = mg_find(tsv, 'P');
                if (mg != nullptr)
                {
                    inScalar = mg->mg_obj;
                    if (sv_isobject(inScalar))
                        tmp = SvIV(SvRV(inScalar));
                }
            }
        }
        else
            tmp = SvIV(SvRV(inScalar));

        if (tmp != 0 and strcmp(kM6ScriptType, HvNAME(SvSTASH(SvRV(inScalar)))) == 0)
            result = reinterpret_cast<M6ParserImpl*>(tmp);
    }

    return result;
}

// ------------------------------------------------------------------

string M6ParserImpl::operator[](const char* inEntry)
{
    string result;

    HV* hash = GetHash();

    if (hash == nullptr)
        THROW(("runtime error"));

    uint32 len = static_cast<uint32>(strlen(inEntry));

    SV** sv = hv_fetch(hash, inEntry, len, 0);
    if (sv != nullptr)
        result = GetString(*sv);

    return result;
}

string M6ParserImpl::GetString(SV* inScalar)
{
    string result;

    if (inScalar != nullptr)
    {
        if (SvPOK(inScalar))
            result = SvPVX(inScalar);
        else
        {
            STRLEN len;
            result = SvPV(inScalar, len);
        }
    }

    return result;
}

// ------------------------------------------------------------------

XS(_M6_Script_new)
{
    dXSARGS;

    SV* obj = newSV(0);
    HV* hash = newHV();

    sv_setref_pv(obj, "M6::Script", M6ParserImpl::sConstructingImp);
    HV* stash = SvSTASH(SvRV(obj));

    GV* gv = *(GV**)hv_fetch(stash, "OWNER", 5, true);
    if (not isGV(gv))
        gv_init(gv, stash, "OWNER", 5, false);

    HV* hv = GvHVn(gv);
    hv_store_ent(hv, obj, newSViv(1), 0);

    sv_magic((SV*)hash, (SV*)obj, 'P', Nullch, 0);
    sv_free(obj);

    SV* sv = sv_newmortal();

    SV* self = newRV_noinc((SV*)hash);
    sv_setsv_flags(sv, self, SV_GMAGIC);
    sv_free((SV*)self);
    sv_bless(sv, stash);

    // copy the hash values into our hash

    for (int i = 0; i < items; i += 2)
    {
        SV* key = ST(i);
        SV* value = ST(i + 1);

        SvREFCNT_inc(value);

        HE* e = hv_store_ent(M6ParserImpl::sConstructingImp->GetHash(), key, value, 0);
        if (e == nullptr)
            sv_free(value);
    }

    ST(0) = sv;

    XSRETURN(1);
}

XS(_M6_Script_DESTROY)
{
    dXSARGS;

    if (items != 1)
        croak("Usage: M6::Script::DESTROY(self);");

    // the parser was deleted

    XSRETURN(0);
}

XS(_M6_Script_FIRSTKEY)
{
    dXSARGS;

    if (items != 1)
        croak("Usage: M6::Script::FIRSTKEY(self);");

    M6ParserImpl* proxy = M6ParserImpl::GetObject(ST(0));

    if (proxy == nullptr)
        croak("Error, M6::Script object is not specified");

    (void)hv_iterinit(proxy->GetHash());
    HE* e = hv_iternext(proxy->GetHash());
    if (e == nullptr)
    {
        ST(0) = sv_newmortal();
        sv_setsv_flags(ST(0), &PL_sv_undef, SV_GMAGIC);
    }
    else
        ST(0) = hv_iterkeysv(e);

    XSRETURN(1);
}

XS(_M6_Script_NEXTKEY)
{
    dXSARGS;

    if (items < 1)
        croak("Usage: M6::Script::NEXTKEY(self);");

    M6ParserImpl* proxy = M6ParserImpl::GetObject(ST(0));

    if (proxy == nullptr)
        croak("Error, M6::Script object is not specified");

    HE* e = hv_iternext(proxy->GetHash());
    if (e == nullptr)
    {
        ST(0) = sv_newmortal();
        sv_setsv_flags(ST(0), &PL_sv_undef, SV_GMAGIC);
    }
    else
        ST(0) = hv_iterkeysv(e);

    XSRETURN(1);
}

XS(_M6_Script_FETCH)
{
    dXSARGS;

    if (items != 2)
        croak("Usage: M6::Script::FETCH(self, name);");

    M6ParserImpl* proxy = M6ParserImpl::GetObject(ST(0));

    if (proxy == nullptr)
        croak("Error, M6::Script object is not specified");

    SV* key = ST(1);

    HE* e = hv_fetch_ent(proxy->GetHash(), key, 0, 0);

    SV* result;

    if (e != nullptr)
    {
        result = HeVAL(e);
        SvREFCNT_inc(result);
    }
    else
    {
        result = sv_newmortal();
        sv_setsv_flags(result, &PL_sv_undef, SV_GMAGIC);
    }

    ST(0) = result;
    XSRETURN(1);
}

XS(_M6_Script_STORE)
{
    dXSARGS;

    if (items != 3)
        croak("Usage: M6::Script::STORE(self, name, value);");

    M6ParserImpl* proxy = M6ParserImpl::GetObject(ST(0));

    if (proxy == nullptr)
        croak("Error, M6::Script object is not specified");

    SV* key = ST(1);
    SV* value = ST(2);

    SvREFCNT_inc(value);

    HE* e = hv_store_ent(proxy->GetHash(), key, value, 0);
    if (e == nullptr)
        sv_free(value);

    XSRETURN(0);
}

XS(_M6_Script_CLEAR)
{
    dXSARGS;

    if (items < 1)
        croak("Usage: M6::Script::CLEAR(self);");

    M6ParserImpl* proxy = M6ParserImpl::GetObject(ST(0));

    if (proxy == nullptr)
        croak("Error, M6::Script object is not specified");

    hv_clear(proxy->GetHash());

    XSRETURN(0);
}

XS(_M6_Script_next_sequence_nr)
{
    dXSARGS;

    if (items != 1)
        croak("Usage: M6::Script::next_sequence_nr(self);");

    M6ParserImpl* proxy = M6ParserImpl::GetObject(ST(0));
    if (proxy == nullptr)
        croak("Error, M6::Script object is not specified");

//    static boost::mutex m;
//    boost::mutex::scoped_lock lock(m);
    static atomic<uint32> counter(1);
    ST(0) = newSViv(counter++);

    XSRETURN(1);
}

XS(_M6_Script_set_attribute)
{
    dXSARGS;

    if (items != 3)
        croak("Usage: M6::Script::set_attribute(self, field, text);");

    M6ParserImpl* proxy = M6ParserImpl::GetObject(ST(0));
    if (proxy == nullptr)
        croak("Error, M6::Script object is not specified");

    const char* ptr;
    STRLEN len;

    ptr = SvPV(ST(1), len);
    if (ptr == nullptr)
        croak("Error, no field defined in call to set_attribute");

    string name(ptr, len);

    ptr = SvPV(ST(2), len);
    if (ptr == nullptr)
        croak("Error, no text defined in call to set_attribute");

    try
    {
        proxy->SetAttribute(name, ptr, len);
    }
    catch (exception& e)
    {
        croak(e.what());
    }

    XSRETURN(0);
}

XS(_M6_Script_set_document)
{
    dXSARGS;

    if (items != 2)
        croak("Usage: M6::Script::set_document(self, text);");

    M6ParserImpl* proxy = M6ParserImpl::GetObject(ST(0));
    if (proxy == nullptr)
        croak("Error, M6::Script object is not specified");

    const char* ptr;
    STRLEN len;

    ptr = SvPV(ST(1), len);
    if (ptr == nullptr)
        croak("Error, no text defined in call to set_document");

    try
    {
        proxy->SetDocument(ptr, len);
    }
    catch (exception& e)
    {
        croak(e.what());
    }

    XSRETURN(0);
}

XS(_M6_Script_index_text)
{
    dXSARGS;

    if (items != 3)
        croak("Usage: M6::Script::index_text(self, indexname, text);");

    M6ParserImpl* proxy = M6ParserImpl::GetObject(ST(0));
    if (proxy == nullptr)
        croak("Error, M6::Script object is not specified");

    // fetch the parameters

    string index;
    const char* ptr;
    STRLEN len;

    ptr = SvPV(ST(1), len);
    if (ptr == nullptr or len == 0)
        croak("Error, indexname is undefined in call to index_text");

    index.assign(ptr, len);

    ptr = SvPV(ST(2), len);
    if (ptr != nullptr and len > 0)
    {
        try
        {
            proxy->IndexText(index, ptr, len);
        }
        catch (exception& e)
        {
            croak(e.what());
        }
    }
//    else if (VERBOSE)
//        cout << "Warning, text is undefined in call to IndexText" << endl;

    XSRETURN(0);
}

XS(_M6_Script_index_string)
{
    dXSARGS;

    if (items != 3)
        croak("Usage: M6::Script::index_string(self, indexname, str);");

    M6ParserImpl* proxy = M6ParserImpl::GetObject(ST(0));
    if (proxy == nullptr)
        croak("Error, M6::Script object is not specified");

    // fetch the parameters

    const char* ptr;
    STRLEN len;

    ptr = SvPV(ST(1), len);
    if (ptr == nullptr or len == 0)
        croak("Error, indexname is undefined in call to index_string");

    string index(ptr, len);

    ptr = SvPV(ST(2), len);
    if (ptr != nullptr and len > 0)
    {
        try
        {
            proxy->IndexString(index, ptr, len, false);
        }
        catch (exception& e)
        {
            croak(e.what());
        }
    }

    XSRETURN(0);
}

XS(_M6_Script_index_unique_string)
{
    dXSARGS;

    if (items != 3)
        croak("Usage: M6::Script::index_unique_string(self, indexname, str);");

    M6ParserImpl* proxy = M6ParserImpl::GetObject(ST(0));
    if (proxy == nullptr)
        croak("Error, M6::Script object is not specified");

    // fetch the parameters

    const char* ptr;
    STRLEN len;

    ptr = SvPV(ST(1), len);
    if (ptr == nullptr or len == 0)
        croak("Error, indexname is undefined in call to index_unique_string");

    string index(ptr, len);

    ptr = SvPV(ST(2), len);
    if (ptr != nullptr and len > 0)
    {
        try
        {
            proxy->IndexString(index, ptr, len, true);
        }
        catch (exception& e)
        {
            croak(e.what());
        }
    }

    XSRETURN(0);
}

XS(_M6_Script_index_number)
{
    dXSARGS;

    if (items != 3)
        croak("Usage: M6::Script::index_number(self, indexname, value);");

    M6ParserImpl* proxy = M6ParserImpl::GetObject(ST(0));
    if (proxy == nullptr)
        croak("Error, M6::Script object is not specified");

    // fetch the parameters

    string index;
    const char* ptr;
    STRLEN len;

    ptr = SvPV(ST(1), len);
    if (ptr == nullptr or len == 0)
        croak("Error, indexname is undefined in call to index_number");

    index.assign(ptr, len);

    ptr = SvPV(ST(2), len);
    if (ptr == nullptr or len == 0)
        croak("Error, value is undefined in call to index_number");

    try
    {
        proxy->IndexNumber(index, ptr, len);
    }
    catch (exception& e)
    {
        croak(e.what());
    }

    XSRETURN(0);
}

XS(_M6_Script_index_float)
{
    dXSARGS;

    if (items != 3)
        croak("Usage: M6::Script::index_float(self, indexname, value);");

    M6ParserImpl* proxy = M6ParserImpl::GetObject(ST(0));
    if (proxy == nullptr)
        croak("Error, M6::Script object is not specified");

    // fetch the parameters

    string index;
    const char* ptr;
    STRLEN len;

    ptr = SvPV(ST(1), len);
    if (ptr == nullptr or len == 0)
        croak("Error, indexname is undefined in call to index_float");

    index.assign(ptr, len);

    ptr = SvPV(ST(2), len);
    if (ptr == nullptr or len == 0)
        croak("Error, value is undefined in call to index_float");

    try
    {
        proxy->IndexFloat(index, ptr, len);
    }
    catch (exception& e)
    {
        croak(e.what());
    }

    XSRETURN(0);
}

XS(_M6_Script_index_date)
{
    dXSARGS;

    if (items != 3)
        croak("Usage: M6::Script::index_date(self, indexname, date);");

    M6ParserImpl* proxy = M6ParserImpl::GetObject(ST(0));
    if (proxy == nullptr)
        croak("Error, M6::Script object is not specified");

    // fetch the parameters

    const char* ptr;
    STRLEN len;

    ptr = SvPV(ST(1), len);
    if (ptr == nullptr or len == 0)
        croak("Error, indexname is undefined in call to index_date");

    string index(ptr, len);

    ptr = SvPV(ST(2), len);
    if (ptr == nullptr or len == 0)
        croak("Error, value is undefined in call to index_date");

    try
    {
        proxy->IndexDate(index, ptr, len);
    }
    catch (exception& e)
    {
        croak(e.what());
    }

    XSRETURN(0);
}

XS(_M6_Script_add_link)
{
    dXSARGS;

    if (items != 3)
        croak("Usage: M6::Script::add_link(self, databank, value);");

    M6ParserImpl* proxy = M6ParserImpl::GetObject(ST(0));
    if (proxy == nullptr)
        croak("Error, M6::Script object is not specified");

    // fetch the parameters

    const char* ptr;
    STRLEN len;

    ptr = SvPV(ST(1), len);
    if (ptr == nullptr or len == 0)
        croak("Error, databank is undefined in call to add_link");

    string databank(ptr, len);

    ptr = SvPV(ST(2), len);
    if (ptr == nullptr or len == 0)
        croak("Error, value is undefined in call to add_link");

    string value(ptr, len);

    try
    {
        proxy->AddLink(databank, value);
    }
    catch (exception& e)
    {
        croak(e.what());
    }

    XSRETURN(0);
}

void xs_init(pTHX)
{
    char *file = const_cast<char*>(__FILE__);
    dXSUB_SYS;

    /* DynaLoader is a special case */
    newXS(const_cast<char*>("DynaLoader::boot_DynaLoader"), boot_DynaLoader, file);

#if defined(_MSC_VER)
    newXS("Win32CORE::bootstrap", boot_Win32CORE, file);
#endif

    // our methods
    newXS(const_cast<char*>("M6::new_M6_Script"), _M6_Script_new, file);
    newXS(const_cast<char*>("M6::delete_M6_Script"), _M6_Script_DESTROY, file);

    newXS(const_cast<char*>("M6::Script::FIRSTKEY"), _M6_Script_FIRSTKEY, file);
    newXS(const_cast<char*>("M6::Script::NEXTKEY"), _M6_Script_NEXTKEY, file);
    newXS(const_cast<char*>("M6::Script::FETCH"), _M6_Script_FETCH, file);
    newXS(const_cast<char*>("M6::Script::STORE"), _M6_Script_STORE, file);
    newXS(const_cast<char*>("M6::Script::CLEAR"), _M6_Script_CLEAR, file);

    newXS(const_cast<char*>("M6::Script::next_sequence_nr"), _M6_Script_next_sequence_nr, file);

    newXS(const_cast<char*>("M6::Script::set_attribute"), _M6_Script_set_attribute, file);
    newXS(const_cast<char*>("M6::Script::set_document"), _M6_Script_set_document, file);

    newXS(const_cast<char*>("M6::Script::index_text"), _M6_Script_index_text, file);
    newXS(const_cast<char*>("M6::Script::index_string"), _M6_Script_index_string, file);
    newXS(const_cast<char*>("M6::Script::index_unique_string"), _M6_Script_index_unique_string, file);
    newXS(const_cast<char*>("M6::Script::index_number"), _M6_Script_index_number, file);
    newXS(const_cast<char*>("M6::Script::index_float"), _M6_Script_index_float, file);
    newXS(const_cast<char*>("M6::Script::index_date"), _M6_Script_index_date, file);

    newXS(const_cast<char*>("M6::Script::add_link"), _M6_Script_add_link, file);

//    // a couple of constants
//    sv_setiv(get_sv("M6::IS_VALUE_INDEX", true),    eIsValue);
//    sv_setiv(get_sv("M6::INDEX_NUMBERS", true),    eIndexNumbers);
//    sv_setiv(get_sv("M6::STORE_AS_META", true),    eStoreAsMetaData);
//    sv_setiv(get_sv("M6::STORE_IDL", true),        eStoreIDL);
//    sv_setiv(get_sv("M6::INDEX_STRING", true),        eIndexString);
}

M6Parser::M6Parser(const string& inName)
    : mName(inName)
{
}

M6Parser::~M6Parser()
{
}

M6ParserImpl* M6Parser::Impl()
{
    // create thread local impl here
    if (mImpl.get() == nullptr)
        mImpl.reset(new M6ParserImpl(mName));

    return mImpl.get();
}

string M6Parser::GetVersion(const string& inSourceConfig)
{
    string result;
    try
    {
        result = Impl()->GetVersion(inSourceConfig);
    }
    catch (exception& e)
    {
        if (VERBOSE)
            cerr << endl << e.what() << endl;
    }
    return result;
}

void M6Parser::ParseDocument(M6InputDocument* inDoc,
    const string& inFileName, const string& inDbHeader)
{
    Impl()->Parse(inDoc, inFileName, inDbHeader);
}

string M6Parser::GetValue(const string& inName)
{
    return Impl()->operator[](inName.c_str());
}

void M6Parser::ToFasta(const string& inDoc, const string& inDb, const string& inID,
    const string& inTitle, string& outFasta)
{
    Impl()->ToFasta(inDoc, inDb, inID, inTitle, outFasta);
}

void M6Parser::GetIndexNames(vector<pair<string,string>>& outIndexNames)
{
    Impl()->GetIndexNames(outIndexNames);
}
