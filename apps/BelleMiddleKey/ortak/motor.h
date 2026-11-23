// BelleMiddleKey tus motoru (saf Symbian, Qt'siz): kisayol listesi, tus yakalama ve tetik algilama.
// Hem arka plan servisi (BelleMiddleKeyServis.exe) hem ayar uygulamasinin test ekrani kullanir.
//
// Dosya: C:\Data\BelleMiddleKey\kisayollar.txt (ASCII), satirlar:
//   titresim 1
//   kisayol <scan hex> <kod hex> <bas|cok|uzun> <sayi> <uygulama|sessiz|kapat|menu|gorev|solsoftkey|sagsoftkey> <param hex>
//
// Yakalama: yalnizca "uzun" atanmis ve tus kodu olan tuslarda CaptureLongKey kullanilir; kisa basis
// odaktaki uygulamaya gider (tus normal calismaya devam eder). Bas/cok atanmis tuslar tamamen yakalanir;
// eslesmeyen basislar servis tarafindan sisteme geri gonderilir.
#ifndef BMK_MOTOR_H
#define BMK_MOTOR_H

#include <e32base.h>
#include <w32std.h>

enum TTetik { ETetikBas = 0, ETetikCok = 1, ETetikUzun = 2 };
enum TEylem {
    EEylemUygulama = 0, EEylemSessiz = 1, EEylemKapat = 2, EEylemMenu = 3, EEylemGorev = 4,
    EEylemSolSoftkey = 5, EEylemSagSoftkey = 6
};

struct TKisayol
{
    TInt iScan;      // tarama kodu (EStdKey...)
    TUint iCode;     // tus kodu (EKey...), 0 = yok
    TInt iTetik;     // TTetik
    TInt iSayi;      // ETetikCok icin 2..5, digerlerinde 1
    TInt iEylem;     // TEylem
    TUint32 iParam;  // EEylemUygulama: uygulama UID'i
};

// Servis <-> uygulama penceresi olaylari (RWsSession::SendEventToWindowGroup)
const TInt KBmkYenile   = EEventUser + 11;   // dosyayi yeniden oku
const TInt KBmkDuraklat = EEventUser + 12;   // tuslari birak (kayit / test sirasinda), 10 dk sonra kendiliginden surer
const TInt KBmkSurdur   = EEventUser + 13;
const TInt KBmkCikis    = EEventUser + 14;
const TUint32 KBmkServisUid = 0xE39CF23E;
const TUint32 KBmkUygulamaUid = 0xE39CF232;

class MMotorDinleyici
{
public:
    virtual void Tetiklendi(const TKisayol &aK) = 0;
    virtual void Eslesmedi(TInt aScan, TUint aCode, TInt aTetik, TInt aSayi) = 0;
};

class CMotor : public CBase
{
public:
    static CMotor *NewL(RWsSession &aWs, RWindowGroup &aWg, MMotorDinleyici &aD);
    ~CMotor();

    void YukleL();                 // dosyadan; dosya yoksa varsayilanlar yazilir
    void KaydetL();
    void Yakala();
    void Birak();
    TBool Yakalandi() const { return iYakalandi; }
    TInt YakalamaHatasi() const { return iHata; }
    TInt YakalananTus() const { return iTusSayisi; }
    TBool OlayL(const TWsEvent &aE);   // ETrue: olay bu motorun (tuketilmeli)
    RArray<TKisayol> &Kisayollar() { return iK; }
    void Varsayilanlar();

    TBool iTitresim;

private:
    CMotor(RWsSession &aWs, RWindowGroup &aWg, MMotorDinleyici &aD);
    void ConstructL();
    void SatirL(const TDesC8 &aS);
    TBool Var(TInt aScan) const;
    TBool Tam(TInt aScan) const;
    TInt EnCok(TInt aScan) const;
    TUint KodBul(TInt aScan) const;
    const TKisayol *Bul(TInt aScan, TInt aTetik, TInt aSayi) const;
    void Coz();
    void Uzun();
    static TInt UzunCb(TAny *aP);
    static TInt AraCb(TAny *aP);

    struct TYakalama { TInt32 iH; TInt iTur; };   // 0 asagi/yukari, 1 tus, 2 uzun

    RWsSession &iWs;
    RWindowGroup &iWg;
    MMotorDinleyici &iD;
    RArray<TKisayol> iK;
    RArray<TYakalama> iY;
    CPeriodic *iUzunZ;
    CPeriodic *iAraZ;
    TBool iYakalandi;
    TInt iHata, iTusSayisi;
    TInt iScan;          // bekleyen dizinin tusu
    TBool iBasili, iUzunOldu;
    TInt iSayac;
    TInt iAsagiScan;     // yalnizca-uzun tuslarda on planda gorulen KeyDown
    TTime iSonUzun;
};

#endif
