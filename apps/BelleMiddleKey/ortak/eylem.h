// BelleMiddleKey eylemleri (saf Symbian): uygulama ac, Sessiz <-> onceki profil, acik uygulamalari kapat,
// Menu tusu (kisa: ana menu, uzun: gorev degistirici).
#ifndef BMK_EYLEM_H
#define BMK_EYLEM_H

#include <e32base.h>
#include <w32std.h>
#include "motor.h"

class Eylem
{
public:
    // Hata kodu dondurur (KErrNone: tamam). Kapat'ta aKapatilan kapatilan uygulama sayisini alir.
    static TInt Calistir(RWsSession &aWs, const TKisayol &aK, TBool aTitresim, TInt *aKapatilan = 0);
    static void Titret(TInt aMs);
private:
    static TInt UygulamaAc(RWsSession &aWs, TUint32 aUid);
    static TInt Sessiz();
    static TInt Kapat(RWsSession &aWs, TInt &aSayi);
    static void SoftkeyBas(RWsSession &aWs, TUint aKod, TInt aScan);
    static void TusBas(RWsSession &aWs, TInt aScan, TInt aSureUs);
};

#endif
