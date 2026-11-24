// BelleGram istemcisi: MTProto baglantisinin ustunde Telegram hesabi (giris, sohbetler, mesajlar, medya, guncellemeler).
// QML'e "tg" olarak verilir. Kimlikler QML'de kesinlik kaybolmasin diye dize: "u123" kisi, "c123" grup, "k123" kanal.
// Oturum (yetki anahtarlari, ana veri merkezi) uygulamanin ozel klasorunde: oturum.ini
// Medya: fotograf/sesli mesaj onbellegi ozel klasorde (medya/), indirilen dosyalar E:/BelleGram
#ifndef TG_TELEGRAM_H
#define TG_TELEGRAM_H

#include <QtCore/QObject>
#include <QtCore/QHash>
#include <QtCore/QSet>
#include <QtCore/QVariant>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QDateTime>

class Baglanti;
class QNetworkSession;

class Telegram : public QObject
{
    Q_OBJECT
    // baslangic | api | baglaniyor | telefon | kod | sifre | hazir
    // api: kullanicinin kendi api_id/api_hash'i (my.telegram.org) henuz girilmedi; koda gomulu kimlik YOK
    Q_PROPERTY(QString durum READ durum NOTIFY durumDegisti)
    Q_PROPERTY(QString apiId READ apiIdMetin NOTIFY durumDegisti)
    Q_PROPERTY(QString benAd READ benAd NOTIFY durumDegisti)
    Q_PROPERTY(QString telefon READ telefon NOTIFY durumDegisti)
    Q_PROPERTY(bool bagli READ bagli NOTIFY bagliDegisti)
    Q_PROPERTY(QString kodBilgi READ kodBilgi NOTIFY durumDegisti)
    Q_PROPERTY(QString sifreIpucu READ sifreIpucu NOTIFY durumDegisti)
public:
    // veriKlasoru: oturum (ozel klasor). gunlukDosyasi: okunabilir gunluk (sifre/anahtar yazilmaz)
    Telegram(const QString &veriKlasoru, const QString &gunlukDosyasi, QObject *parent = 0);
    ~Telegram();

    QString durum() const { return m_durum; }
    QString benAd() const { return m_benAd; }
    QString telefon() const { return m_telefon; }
    bool bagli() const { return m_bagli; }
    QString kodBilgi() const { return m_kodBilgi; }
    QString sifreIpucu() const { return m_ipucu; }
    QString apiIdMetin() const { return m_apiId ? QString::number(m_apiId) : QString(); }

    // giris
    Q_INVOKABLE void basla();
    // api_id/api_hash kaydet (ozel klasorde kimlik.ini; cikista silinmez). Hata varsa metnini dondurur, yoksa ""
    Q_INVOKABLE QString apiAyarla(const QString &id, const QString &hash);
    // API bilgilerini degistir: oturumu kapatir, kimligi siler, API ekranina doner
    Q_INVOKABLE void apiDegistir();
    Q_INVOKABLE void kodGonder(const QString &telefon);
    Q_INVOKABLE void kodOnayla(const QString &kod);
    Q_INVOKABLE void sifreOnayla(const QString &sifre);
    Q_INVOKABLE void numaraDegistir();
    Q_INVOKABLE void cikis();
    // sohbetler
    Q_INVOKABLE void sohbetleriYukle(bool daha);
    Q_INVOKABLE void sessizAyarla(const QString &sohbet, bool sessiz);
    Q_INVOKABLE void sabitle(const QString &sohbet, bool sabit);
    Q_INVOKABLE void sohbetSil(const QString &sohbet);
    Q_INVOKABLE QString sohbetAdi(const QString &sohbet) const;
    Q_INVOKABLE QString kisiDurumu(const QString &sohbet) const;
    Q_INVOKABLE QString kayitliSohbet() const { return QString::fromLatin1("u") + QString::number(m_benId); }
    // mesajlar
    Q_INVOKABLE void mesajlariYukle(const QString &sohbet, bool eskiler);
    Q_INVOKABLE void mesajGonder(const QString &sohbet, const QString &metin, const QString &yanitId = QString());
    Q_INVOKABLE void mesajDuzenle(const QString &sohbet, const QString &id, const QString &metin);
    Q_INVOKABLE void mesajSil(const QString &sohbet, const QString &id, bool herkesten);
    Q_INVOKABLE void ilet(const QString &kaynak, const QString &id, const QString &hedef);
    Q_INVOKABLE void tepkiGonder(const QString &sohbet, const QString &id, const QString &emoji);
    Q_INVOKABLE void okundu(const QString &sohbet);
    Q_INVOKABLE void yaziyor(const QString &sohbet, bool ses = false);
    // kisiler / arama
    Q_INVOKABLE void kisileriYukle();
    Q_INVOKABLE void kisiAra(const QString &sorgu);
    // medya
    Q_INVOKABLE void medyaIndir(const QString &anahtar);
    Q_INVOKABLE QString medyaYolu(const QString &anahtar) const;
    Q_INVOKABLE QString medyaKaydet(const QString &anahtar);
    Q_INVOKABLE void fotoGonder(const QString &sohbet, const QString &yol, const QString &aciklama = QString(), const QString &yanitId = QString());
    Q_INVOKABLE void dosyaGonder(const QString &sohbet, const QString &yol, const QString &yanitId = QString());
    Q_INVOKABLE void sesGonder(const QString &sohbet, const QString &yol, int sure, const QString &dalgaHex, const QString &yanitId = QString());
    Q_INVOKABLE void konumGonder(const QString &sohbet, double lat, double lng, const QString &ad, const QString &adres);
    Q_INVOKABLE QString gunluk() const { return m_gunluk.join(QString::fromLatin1("\n")); }

signals:
    void durumDegisti();
    void bagliDegisti();
    void hataOldu(const QString &mesaj);
    void bilgi(const QString &mesaj);
    void sohbetListesi(const QVariantList &liste, bool daha, bool sonu);
    void sohbetGuncellendi(const QVariantMap &sohbet);
    void sohbetOzellik(const QString &sohbet, const QString &ad, const QVariant &deger);
    void sohbetSilindi(const QString &sohbet);
    void mesajListesi(const QString &sohbet, const QVariantList &liste, bool eskiler, bool sonu);
    void yeniMesaj(const QString &sohbet, const QVariantMap &mesaj);
    void mesajGitti(const QString &sohbet, const QString &geciciId, const QVariantMap &mesaj);
    void mesajGitmedi(const QString &sohbet, const QString &geciciId, const QString &neden);
    void mesajDuzenlendi(const QString &sohbet, const QVariantMap &mesaj);
    void mesajlarSilindi(const QString &sohbet, const QVariantList &kimlikler);   // sohbet "" : kanal disi herhangi biri
    void okunduBilgi(const QString &sohbet, int enBuyuk);
    void tepkiDegisti(const QString &sohbet, const QString &id, const QString &tepki, const QString &benim);
    void yaziyorBilgi(const QString &sohbet, const QString &metin);
    void kisiDurumDegisti(const QString &sohbet);
    void kisiListesi(const QVariantList &liste);
    void aramaSonucu(const QString &sorgu, const QVariantList &liste);
    void medyaHazir(const QString &anahtar, const QString &yol);
    void medyaHata(const QString &anahtar, const QString &mesaj);
    void medyaIlerleme(const QString &anahtar, int yuzde);
    void yuklemeIlerleme(const QString &sohbet, const QString &geciciId, int yuzde);

private slots:
    void nabiz();                    // 5 dakikada bir bellek / liste boyutu gunluge (cokme teshisi)
    void gunlukYaz();                // biriken gunluk satirlarini dosyaya toplu yaz
    void agAcildi();
    void anahtarHazir();
    void sonuc(int istek, const QVariant &v);
    void hata(int istek, int kod, const QString &mesaj);
    void guncelleme(const QVariant &v);
    void koptu(const QString &neden);
    void yenidenBaglan();
    void yeniOturum();
    void kayit(const QString &s);
    // medya veri merkezleri
    void dcAnahtar();
    void dcSonuc(int istek, const QVariant &v);
    void dcHata(int istek, int kod, const QString &mesaj);
    void dcKoptu(const QString &neden);
    void dcYeniden();
    void indirmeZamanAsimi();

private:
    enum IsTuru { IsKod, IsImza, IsSifreBilgi, IsSifre, IsDurum, IsSohbet, IsGecmis, IsGonder, IsOkundu, IsCikis,
                  IsBos, IsGuncelleme, IsKisiler, IsAra, IsDosya, IsDisaAktar, IsIceAktar, IsParca };
    struct Is { Is() : tur(IsBos), ek(0), daha(false) {} int tur; QString sohbet; qint64 ek; bool daha; QString metin; };
    // Indirme parcalari dogrudan <hedef>.part dosyasina yazilir (bellekte tutulmaz)
    struct Indirme { Indirme() : dc(0), boyut(0), ses(false), deneme(0), alinan(0), parcaBoy(131072) {} QString anahtar; int dc; QVariantMap konum; qint64 boyut; QString hedef; bool ses; int deneme; qint64 alinan; int parcaBoy; };
    // Dosya gonderiminde veri bos, yol dolu: her parca dosyadan okunur
    struct Yukleme {
        Yukleme() : dosyaId(0), parca(0), toplam(0), buyuk(false), sure(0), rastgele(0), boyut(0), parcaBoy(131072) {}
        QString sohbet, gecici, ad, mime, aciklama, yanit, tur, yol;
        QByteArray veri, dalga;
        qint64 dosyaId; int parca, toplam; bool buyuk; int sure; qint64 rastgele; qint64 boyut; int parcaBoy;
    };

    void durumYap(const QString &d);
    void baglantiKur(int dc);
    int cagir(const QString &yontem, const QVariantMap &p, int tur, const QString &sohbet = QString(), qint64 ek = 0, bool daha = false);
    void girisTamam(const QVariantMap &user);
    void sifreHesapla(const QVariantMap &parolaBilgi);
    void kaydet();
    void apiOku();
    void initAyarla(Baglanti *b) const;
    // onbellek
    void kisileriAl(const QVariantList &users);
    void sohbetleriAl(const QVariantList &chats);
    QString anahtar(const QVariant &peer) const;
    QVariantMap girdiPeer(const QString &sohbet) const;
    QVariantMap girdiKanal(const QString &sohbet) const;
    QVariantMap mesajHarita(const QVariantMap &m) const;
    void yanitDoldur(QVariantMap &h, const QString &sohbet) const;
    QString mesajOzet(const QVariantMap &h) const;
    QVariantMap kisiOge(qint64 id) const;
    QVariantMap sohbetOge(const QString &key) const;
    void yeniMesajIsle(const QVariantMap &m);
    void tekGuncelleme(const QVariantMap &u);
    QString yaziyorMetni(const QVariantMap &action) const;
    static QString hataMetni(const QString &kod);
    qint64 rastgeleId() const;
    QVariantMap yanitGirdi(const QString &yanitId) const;
    QVariantMap geciciMesaj(const QString &sohbet, const QString &gecici, const QString &tur, const QString &yanitId) const;
    // medya
    QString medyaKlasoru() const;
    QString indirmeKlasoru() const;
    void medyaKaydiEkle(const QString &anahtar, int dc, const QVariantMap &konum, qint64 boyut, const QString &hedef, bool ses) const;
    QString sonDosya(const QString &anahtar) const;
    void indirmeBaslat();
    void parcaIste();
    void dosyaParcasi(const Is &is, const QVariantMap &m);
    void indirmeBitti(const QString &hata);
    Baglanti *dcBaglanti(int dc);
    bool dcYetkili(int dc) const;
    int dcCagir(int dc, const QString &yontem, const QVariantMap &p, int tur, qint64 ek = 0);
    QString yuklemeEkle(Yukleme y);
    void yuklemeBaslat();
    void parcaGonder();
    void yuklemeBitti(const QString &hata);
    void dcIsSonucu(int dc, const Is &is, const QVariant &v);

    QString m_klasor, m_gunlukDosya, m_durum, m_benAd, m_telefon, m_kodHash, m_kodBilgi, m_ipucu, m_bekleyenKod;
    QString m_sonKodNumara;      // cift kod istegi korumasi (kodGonder)
    QDateTime m_sonKodIstegi;
    QString m_parola;
    int m_apiId;
    QString m_apiHash;
    qint64 m_benId;
    int m_anaDc;
    bool m_bagli, m_girisli, m_baglantiIstendi;
    Baglanti *m_b;
    QNetworkSession *m_ag;
    QHash<int, Is> m_isler;
    QHash<qint64, QVariantMap> m_kisiler;     // user id -> user
    QHash<qint64, QVariantMap> m_gruplar;     // chat/channel id -> chat
    QHash<QString, int> m_enEski, m_enYeni;   // sohbet -> mesaj kimligi
    QHash<QString, int> m_okunanCikis;        // sohbet -> karsi tarafin okudugu en buyuk mesaj
    QHash<qint64, QString> m_rastgele;        // random_id -> "sohbet|geciciId"
    QSet<QString> m_gorulen;                  // "sohbet:id"
    mutable QHash<QString, QVariantMap> m_medya;   // medya anahtari -> konum bilgisi
    mutable QHash<QString, QVariantMap> m_ozet;    // "sohbet:id" -> {ad, ozet} (yanit alintilari)
    // sohbet listesi sayfalama
    int m_sonTarih, m_sonMesaj;
    QVariantMap m_sonPeer;
    QStringList m_gunluk;
    int m_yenidenDeneme;
    // medya veri merkezleri
    QHash<int, Baglanti *> m_dcB;
    QSet<int> m_dcHazir;
    QHash<qint64, Is> m_dcIsler;              // (dc << 32 | istek)
    // indirme / yukleme kuyrugu (tek tek)
    QList<Indirme> m_indKuyruk;
    Indirme m_ind;
    bool m_indAktif;
    QTimer m_indZaman;
    // cokme teshisi
    QTimer m_nabiz;
    QTimer m_gunlukZaman;            // toplu gunluk yazma (en fazla dakikada bir)
    QStringList m_yazilacak;
    bool m_oncekiCokme;
    QDateTime m_sonNabiz;
    qint64 m_sonCpu;
    QString isaretDosyasi() const;
    QString bellekOzeti() const;
    QList<Yukleme> m_yukKuyruk;
    Yukleme m_yuk;
    bool m_yukAktif;
};

#endif
