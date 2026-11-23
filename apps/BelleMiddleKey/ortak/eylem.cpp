#include "eylem.h"
#include <e32keys.h>
#include <apgtask.h>
#include <apgwgnam.h>
#include <apgcli.h>
#include <apacmdln.h>
#include <proengfactory.h>
#include <mproengengine.h>
#include <hwrmvibra.h>

static const TInt KSessizId = 1;   // Symbian profil kimlikleri: 0 Genel, 1 Sessiz
static const TInt KGenelId = 0;
static TInt sOncekiProfil = KGenelId;
static CHWRMVibra *sTitresim = 0;  // ilk titresimde olusur, surec boyunca kalir

// "Kapat"ta dokunulmayacak uygulamalar (gizli ve sistem pencereleri zaten atlanir)
static const TUint32 KKoru[] = {
    0x100058B3, // Telefon
    0x102750F0, // Ana ekran
    0x20022F35, // Ana ekran (Belle)
    0x101F4CD2, // Menu
    KBmkUygulamaUid,
    KBmkServisUid
};

void Eylem::Titret(TInt aMs)
{
    if (!sTitresim) TRAP_IGNORE(sTitresim = CHWRMVibra::NewL());
    if (sTitresim) TRAP_IGNORE(sTitresim->StartVibraL(aMs));
}

TInt Eylem::Calistir(RWsSession &aWs, const TKisayol &aK, TBool aTitresim, TInt *aKapatilan)
{
    TInt err = KErrNone;
    switch (aK.iEylem) {
    case EEylemUygulama: err = UygulamaAc(aWs, aK.iParam); break;
    case EEylemSessiz: err = Sessiz(); break;
    case EEylemKapat: { TInt n = 0; err = Kapat(aWs, n); if (aKapatilan) *aKapatilan = n; break; }
    case EEylemMenu: TusBas(aWs, EStdKeyApplication0, 60000); break;
    case EEylemGorev: TusBas(aWs, EStdKeyApplication0, 1500000); break;
    case EEylemSolSoftkey: SoftkeyBas(aWs, EKeyDevice0, EStdKeyDevice0); break;
    case EEylemSagSoftkey: SoftkeyBas(aWs, EKeyDevice1, EStdKeyDevice1); break;
    default: err = KErrNotSupported;
    }
    if (aTitresim && err == KErrNone) Titret(50);
    return err;
}

TInt Eylem::UygulamaAc(RWsSession &aWs, TUint32 aUid)
{
    const TUid uid = TUid::Uid(aUid);
    TApaTaskList tl(aWs);
    TApaTask task = tl.FindApp(uid);
    if (task.Exists()) { task.BringToForeground(); return KErrNone; }
    RApaLsSession ls;
    TInt err = ls.Connect();
    if (err != KErrNone) return err;
    TApaAppInfo info;
    err = ls.GetAppInfo(info, uid);
    if (err == KErrNone) {
        CApaCommandLine *cmd = 0;
        TRAP(err, {
            cmd = CApaCommandLine::NewL();
            cmd->SetExecutableNameL(info.iFullName);
            cmd->SetCommandL(EApaCommandRun);
        });
        if (err == KErrNone) err = ls.StartApp(*cmd);
        delete cmd;
    }
    ls.Close();
    return err;
}

TInt Eylem::Sessiz()
{
    MProEngEngine *eng = 0;
    TRAPD(err, eng = ProEngFactory::NewEngineL());
    if (err != KErrNone || !eng) return err ? err : KErrGeneral;
    const TInt cur = eng->ActiveProfileId();
    TInt hedef;
    if (cur == KSessizId) hedef = (sOncekiProfil != KSessizId) ? sOncekiProfil : KGenelId;
    else { sOncekiProfil = cur; hedef = KSessizId; }
    TRAP(err, eng->SetActiveProfileL(hedef));
    eng->Release();
    if (err == KErrNone && hedef != KSessizId) Titret(120);   // sesli profile donuldu
    return err;
}

TInt Eylem::Kapat(RWsSession &aWs, TInt &aSayi)
{
    aSayi = 0;
    CArrayFixFlat<TInt> *wgs = new CArrayFixFlat<TInt>(16);
    if (!wgs) return KErrNoMemory;
    TInt err = aWs.WindowGroupList(wgs);
    if (err != KErrNone) { delete wgs; return err; }
    for (TInt i = 0; i < wgs->Count(); ++i) {
        const TInt id = wgs->At(i);
        CApaWindowGroupName *ad = 0;
        TRAPD(e, ad = CApaWindowGroupName::NewL(aWs, id));
        if (e != KErrNone || !ad) continue;
        TBool atla = ad->Hidden() || ad->IsSystem();
        const TUint32 uid = ad->AppUid().iUid;
        if (uid == 0) atla = ETrue;
        for (TUint k = 0; k < sizeof(KKoru) / sizeof(KKoru[0]); ++k) if (uid == KKoru[k]) atla = ETrue;
        delete ad;
        if (atla) continue;
        TApaTask task(aWs);
        task.SetWgId(id);
        task.EndTask();
        ++aSayi;
    }
    delete wgs;
    return KErrNone;
}

void Eylem::SoftkeyBas(RWsSession &aWs, TUint aKod, TInt aScan)
{
    // Mantiksal tus olayi, cihaz klavyesinin scan -> keycode eslemesine bagli degil.
    TKeyEvent ev;
    ev.iCode = aKod;
    ev.iScanCode = aScan;
    ev.iModifiers = 0;
    ev.iRepeats = 0;
    aWs.SimulateKeyEvent(ev);
    aWs.Flush();
}

void Eylem::TusBas(RWsSession &aWs, TInt aScan, TInt aSureUs)
{
    TRawEvent ev;
    ev.Set(TRawEvent::EKeyDown, aScan);
    aWs.SimulateRawEvent(ev);
    aWs.Flush();
    User::After(aSureUs);
    ev.Set(TRawEvent::EKeyUp, aScan);
    aWs.SimulateRawEvent(ev);
    aWs.Flush();
}
