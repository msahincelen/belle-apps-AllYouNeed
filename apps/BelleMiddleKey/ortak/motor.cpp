#include "motor.h"
#include <e32keys.h>
#include <f32file.h>

_LIT(KKlasor, "C:\\Data\\BelleMiddleKey\\");
_LIT(KDosya, "C:\\Data\\BelleMiddleKey\\kisayollar.txt");
_LIT8(KTitresim, "titresim");
_LIT8(KKisayol, "kisayol");
_LIT8(KBas, "bas");
_LIT8(KCok, "cok");
_LIT8(KUzunAd, "uzun");
_LIT8(KUygulama, "uygulama");
_LIT8(KSessiz, "sessiz");
_LIT8(KKapat, "kapat");
_LIT8(KMenu, "menu");
_LIT8(KGorev, "gorev");
_LIT8(KSolSoftkey, "solsoftkey");
_LIT8(KSagSoftkey, "sagsoftkey");

static TBool SoftkeyMi(TInt aScan, TUint aCode)
{
    return aScan == EStdKeyDevice0 || aScan == EStdKeyDevice1
        || aCode == EKeyDevice0 || aCode == EKeyDevice1;
}

static const TInt KUzunUs = 700000;   // basili tutma esigi
static const TInt KAraUs = 400000;    // pes pese basislar arasi en fazla bekleme
static const TInt KYuksek = KMaxTInt;

CMotor *CMotor::NewL(RWsSession &aWs, RWindowGroup &aWg, MMotorDinleyici &aD)
{
    CMotor *m = new (ELeave) CMotor(aWs, aWg, aD);
    CleanupStack::PushL(m);
    m->ConstructL();
    CleanupStack::Pop(m);
    return m;
}

CMotor::CMotor(RWsSession &aWs, RWindowGroup &aWg, MMotorDinleyici &aD)
    : iTitresim(ETrue), iWs(aWs), iWg(aWg), iD(aD), iYakalandi(EFalse), iHata(0), iTusSayisi(0),
      iScan(-1), iBasili(EFalse), iUzunOldu(EFalse), iSayac(0), iAsagiScan(-1)
{
}

void CMotor::ConstructL()
{
    iUzunZ = CPeriodic::NewL(CActive::EPriorityHigh);
    iAraZ = CPeriodic::NewL(CActive::EPriorityHigh);
}

CMotor::~CMotor()
{
    Birak();
    delete iUzunZ;
    delete iAraZ;
    iK.Close();
    iY.Close();
}

// ------------------------------------------------------------------ dosya
void CMotor::Varsayilanlar()
{
    // Eski OrtaTus davranisi: orta tus (EStdKeyDevice6 / EKeyDevice6)
    iK.Reset();
    TKisayol k;
    k.iScan = EStdKeyDevice6; k.iCode = EKeyDevice6; k.iParam = 0; k.iSayi = 1;
    k.iTetik = ETetikUzun; k.iEylem = EEylemSessiz; iK.Append(k);
    k.iTetik = ETetikBas; k.iEylem = EEylemUygulama; k.iParam = 0x101F857A; iK.Append(k);   // Kamera
    k.iTetik = ETetikCok; k.iSayi = 2; k.iEylem = EEylemKapat; k.iParam = 0; iK.Append(k);
    iTitresim = ETrue;
}

void CMotor::YukleL()
{
    iK.Reset();
    iTitresim = ETrue;
    RFs fs;
    User::LeaveIfError(fs.Connect());
    CleanupClosePushL(fs);
    RFile f;
    if (f.Open(fs, KDosya, EFileRead | EFileShareReadersOnly) != KErrNone) {
        CleanupStack::PopAndDestroy(&fs);
        Varsayilanlar();
        KaydetL();
        return;
    }
    CleanupClosePushL(f);
    TInt boy = 0;
    f.Size(boy);
    if (boy > 16384) boy = 16384;
    HBufC8 *b = HBufC8::NewLC(boy + 1);
    TPtr8 p = b->Des();
    User::LeaveIfError(f.Read(p));
    TInt bas = 0;
    for (TInt i = 0; i <= p.Length(); ++i) {
        if (i == p.Length() || p[i] == '\n') {
            SatirL(p.Mid(bas, i - bas));
            bas = i + 1;
        }
    }
    CleanupStack::PopAndDestroy(3);   // b, f, fs
}

void CMotor::SatirL(const TDesC8 &aS)
{
    TLex8 l(aS);
    TPtrC8 ad = l.NextToken();
    if (ad == KTitresim) {
        TInt v = 1;
        l.SkipSpace();
        l.Val(v);
        iTitresim = v != 0;
        return;
    }
    if (ad != KKisayol) return;
    TKisayol k;
    TUint32 v = 0;
    l.SkipSpace();
    if (l.Val(v, EHex) != KErrNone) return;
    k.iScan = (TInt)v;
    l.SkipSpace();
    if (l.Val(v, EHex) != KErrNone) return;
    k.iCode = v;
    TPtrC8 t = l.NextToken();
    k.iTetik = (t == KUzunAd) ? ETetikUzun : (t == KCok) ? ETetikCok : ETetikBas;
    TInt n = 1;
    l.SkipSpace();
    l.Val(n);
    k.iSayi = (k.iTetik == ETetikCok) ? Max(2, Min(n, 5)) : 1;
    TPtrC8 e = l.NextToken();
    k.iEylem = (e == KSessiz) ? EEylemSessiz : (e == KKapat) ? EEylemKapat : (e == KMenu) ? EEylemMenu
             : (e == KGorev) ? EEylemGorev : (e == KSolSoftkey) ? EEylemSolSoftkey
             : (e == KSagSoftkey) ? EEylemSagSoftkey : EEylemUygulama;
    v = 0;
    l.SkipSpace();
    l.Val(v, EHex);
    k.iParam = v;
    iK.AppendL(k);
}

void CMotor::KaydetL()
{
    RFs fs;
    User::LeaveIfError(fs.Connect());
    CleanupClosePushL(fs);
    fs.MkDirAll(KKlasor);
    RFile f;
    User::LeaveIfError(f.Replace(fs, KDosya, EFileWrite));
    CleanupClosePushL(f);
    TBuf8<200> s;
    s.Copy(_L8("# BelleMiddleKey: kisayol <scan> <kod> <bas|cok|uzun> <sayi> <uygulama|sessiz|kapat|menu|gorev|solsoftkey|sagsoftkey> <param>\r\n"));
    f.Write(s);
    s.Format(_L8("titresim %d\r\n"), iTitresim ? 1 : 0);
    f.Write(s);
    for (TInt i = 0; i < iK.Count(); ++i) {
        const TKisayol &k = iK[i];
        TPtrC8 t(k.iTetik == ETetikUzun ? KUzunAd() : k.iTetik == ETetikCok ? KCok() : KBas());
        TPtrC8 e(k.iEylem == EEylemSessiz ? KSessiz() : k.iEylem == EEylemKapat ? KKapat()
               : k.iEylem == EEylemMenu ? KMenu() : k.iEylem == EEylemGorev ? KGorev()
               : k.iEylem == EEylemSolSoftkey ? KSolSoftkey()
               : k.iEylem == EEylemSagSoftkey ? KSagSoftkey() : KUygulama());
        s.Format(_L8("kisayol %x %x %S %d %S %x\r\n"), k.iScan, k.iCode, &t, k.iSayi, &e, k.iParam);
        f.Write(s);
    }
    CleanupStack::PopAndDestroy(2);
}

// ------------------------------------------------------------------ sorgular
TBool CMotor::Var(TInt aScan) const
{
    for (TInt i = 0; i < iK.Count(); ++i) if (iK[i].iScan == aScan) return ETrue;
    return EFalse;
}

// Tam yakalama gereken tus: bas/cok atanmis ya da tus kodu yok
TBool CMotor::Tam(TInt aScan) const
{
    for (TInt i = 0; i < iK.Count(); ++i)
        if (iK[i].iScan == aScan && (iK[i].iTetik != ETetikUzun || iK[i].iCode == 0)) return ETrue;
    return EFalse;
}

TInt CMotor::EnCok(TInt aScan) const
{
    TInt n = 1;
    for (TInt i = 0; i < iK.Count(); ++i)
        if (iK[i].iScan == aScan && iK[i].iTetik == ETetikCok && iK[i].iSayi > n) n = iK[i].iSayi;
    return n;
}

TUint CMotor::KodBul(TInt aScan) const
{
    for (TInt i = 0; i < iK.Count(); ++i) if (iK[i].iScan == aScan && iK[i].iCode) return iK[i].iCode;
    return 0;
}

const TKisayol *CMotor::Bul(TInt aScan, TInt aTetik, TInt aSayi) const
{
    for (TInt i = 0; i < iK.Count(); ++i) {
        const TKisayol &k = iK[i];
        if (k.iScan == aScan && k.iTetik == aTetik && (aTetik != ETetikCok || k.iSayi == aSayi)) return &k;
    }
    return 0;
}

// ------------------------------------------------------------------ yakalama
void CMotor::Yakala()
{
    if (iYakalandi) return;
    iHata = 0;
    iTusSayisi = 0;
    for (TInt i = 0; i < iK.Count(); ++i) {
        const TInt sc = iK[i].iScan;
        TBool once = EFalse;
        for (TInt j = 0; j < i; ++j) if (iK[j].iScan == sc) once = ETrue;
        if (once) continue;
        const TUint kod = KodBul(sc);
        // Ayar ekrani gibi softkey'leri yakalama; uretilen olaylar odaktaki uygulamaya gitsin.
        if (SoftkeyMi(sc, kod)) continue;
        ++iTusSayisi;
        TYakalama y;
        if (Tam(sc)) {
            y.iTur = 0; y.iH = iWg.CaptureKeyUpAndDowns(sc, 0, 0, KYuksek);
            if (y.iH > 0) iY.Append(y); else ++iHata;
            if (kod) {
                y.iTur = 1; y.iH = iWg.CaptureKey(kod, 0, 0, KYuksek);
                if (y.iH > 0) iY.Append(y); else ++iHata;
            }
        }
        if (kod) {
            // tam yakalanan tusta da uzun basis sisteme (or. sesli komut) gitmesin
            y.iTur = 2; y.iH = iWg.CaptureLongKey(kod, kod, 0, 0, KYuksek, ELongCaptureNormal);
            if (y.iH > 0) iY.Append(y); else ++iHata;
        }
    }
    iYakalandi = ETrue;
}

void CMotor::Birak()
{
    for (TInt i = 0; i < iY.Count(); ++i) {
        const TYakalama &y = iY[i];
        if (y.iTur == 0) iWg.CancelCaptureKeyUpAndDowns(y.iH);
        else if (y.iTur == 1) iWg.CancelCaptureKey(y.iH);
        else iWg.CancelCaptureLongKey(y.iH);
    }
    iY.Reset();
    if (iUzunZ) iUzunZ->Cancel();
    if (iAraZ) iAraZ->Cancel();
    iBasili = EFalse; iUzunOldu = EFalse; iSayac = 0; iScan = -1; iAsagiScan = -1;
    iYakalandi = EFalse;
}

// ------------------------------------------------------------------ algilama
TBool CMotor::OlayL(const TWsEvent &aE)
{
    const TInt t = aE.Type();
    if (t != EEventKey && t != EEventKeyDown && t != EEventKeyUp) return EFalse;
    const TKeyEvent &k = *aE.Key();
    if (SoftkeyMi(k.iScanCode, t == EEventKey ? k.iCode : 0)) return EFalse;

    if (t == EEventKey) {
        TInt s = -1;
        for (TInt i = 0; i < iK.Count(); ++i) if (iK[i].iCode && iK[i].iCode == k.iCode) { s = iK[i].iScan; break; }
        if (s < 0 && Var(k.iScanCode)) s = k.iScanCode;
        if (s < 0) return EFalse;
        if (Tam(s)) return ETrue;   // tam yakalanan tus: zamanlayicilar karar verir
        // yalnizca "uzun": servis yalnizca uzun olayi alir; on plandaki test ekrani kisa basisi da gorur
        if (k.iRepeats > 0 || iAsagiScan != s) {
            TTime simdi;
            simdi.UniversalTime();
            if (simdi.MicroSecondsFrom(iSonUzun).Int64() > 800000) {
                iSonUzun = simdi;
                const TKisayol *b = Bul(s, ETetikUzun, 1);
                if (b) iD.Tetiklendi(*b);
            }
            return ETrue;
        }
        return EFalse;
    }

    const TInt sc = k.iScanCode;
    if (!Var(sc)) return EFalse;
    if (!Tam(sc)) {
        iAsagiScan = (t == EEventKeyDown) ? sc : -1;
        return EFalse;
    }
    if (t == EEventKeyDown) {
        if (iBasili && iScan == sc) return ETrue;          // tekrar
        if (iScan != sc && iSayac > 0) Coz();              // onceki tusun bekleyen basislari
        iScan = sc;
        iBasili = ETrue;
        iUzunOldu = EFalse;
        iAraZ->Cancel();
        if (Bul(sc, ETetikUzun, 1) || EnCok(sc) == 1) {
            iUzunZ->Cancel();
            iUzunZ->Start(KUzunUs, KUzunUs, TCallBack(UzunCb, this));
        }
        return ETrue;
    }
    // EEventKeyUp
    if (!iBasili || iScan != sc) return ETrue;
    iBasili = EFalse;
    iUzunZ->Cancel();
    if (iUzunOldu) { iUzunOldu = EFalse; return ETrue; }
    ++iSayac;
    if (iSayac >= EnCok(sc)) Coz();
    else { iAraZ->Cancel(); iAraZ->Start(KAraUs, KAraUs, TCallBack(AraCb, this)); }
    return ETrue;
}

TInt CMotor::UzunCb(TAny *aP) { static_cast<CMotor *>(aP)->Uzun(); return 0; }
TInt CMotor::AraCb(TAny *aP) { CMotor *m = static_cast<CMotor *>(aP); m->iAraZ->Cancel(); m->Coz(); return 0; }

void CMotor::Uzun()
{
    iUzunZ->Cancel();
    if (!iBasili) return;
    iUzunOldu = ETrue;
    iSayac = 0;
    iAraZ->Cancel();
    const TKisayol *b = Bul(iScan, ETetikUzun, 1);
    if (b) iD.Tetiklendi(*b);
    else iD.Eslesmedi(iScan, KodBul(iScan), ETetikUzun, 1);
}

void CMotor::Coz()
{
    const TInt n = iSayac;
    iSayac = 0;
    iAraZ->Cancel();
    if (n <= 0 || iScan < 0) return;
    const TKisayol *b = (n == 1) ? Bul(iScan, ETetikBas, 1) : Bul(iScan, ETetikCok, n);
    if (b) iD.Tetiklendi(*b);
    else iD.Eslesmedi(iScan, KodBul(iScan), n == 1 ? ETetikBas : ETetikCok, n);
}
