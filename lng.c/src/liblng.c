/**
 * liblng.c - Summary of the functionality
 * Summary: Core implementation for lng language detection.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "liblng.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <stddef.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif
#ifndef _WIN32
#  include <pthread.h>
#endif

#define KC_LNG_NG_SIZE   3
#define KC_LNG_MAX_GRAMS 2048
#define KC_LNG_MAX_LANGS 32

typedef struct {
    char gram[12];
    int count;
} kc_lng_gram_t;

typedef struct {
    const char *code;
    const char *seed;
    kc_lng_gram_t profile[KC_LNG_MAX_GRAMS];
    int profile_size;
    long total;
} kc_lng_lang_t;

typedef struct {
    const char *code;
    double score;
} kc_lng_rank_t;

#define KC_LNG_LANG(code_, seed_) \
    { \
        .code = code_, \
        .seed = seed_, \
        .profile = {{{0}, 0}}, \
        .profile_size = 0, \
        .total = 0 \
    }

static kc_lng_lang_t kc_lng_langs[KC_LNG_MAX_LANGS] = {
    KC_LNG_LANG("en", "the and are for that with this have from they which would there their about which into through across because between world hello morning everyone project matches short text quick brown fox jumps over lazy dog. how are you doing today? this is a robust test for english language detection. documentation is vital for understanding systems. university technology science information research development program software computer system network data business management education government country people year time way service work life world school high school state city area group problem hand part child eye woman place case week company system line question work night world. i love coding in c and python."),
    KC_LNG_LANG("es", "que el la de en que lo los un una por para como al su sus con del por sobre entre mucho después también siempre mundo hola buenos días todos ¿cómo estás? este proyecto compara texto corto el zorro marrón salta sobre el perro. esperanza y libertad para todos los pueblos. la programación es un arte que requiere paciencia. el lenguaje de programación c es muy potente. españa méxico argentina colombia chile perú venezuela ecuador guatemala cuba bolivia república dominicana honduras paraguay el salvador nicaragua costa rica puerto rico panamá uruguay. me gusta mucho viajar y conocer gente nueva. vive habita animal perro gato caballo vaca cerdo oveja pájaro pez. la naturaleza es hermosa. hablar caminar correr estudiar trabajar pensar querer poder saber conocer llegar salir volver poner tomar llevar seguir encontrar llamar. casa calle ciudad pais tiempo lugar persona gente familia vida agua comida dinero libro escuela trabajo empresa. instalar descargar abrir cerrar guardar copiar mover borrar buscar ejecutar actualizar configurar crear eliminar renombrar. programa archivo carpeta sistema red pantalla teclado datos informacion usuario contrasena memoria procesador. cultura historia ciencia salud justicia comunidad mercado musica teatro deporte agricultura medicina energia transporte universidad biblioteca periodico conversacion recuerdo decision primavera invierno amistad ciudadania infancia memoria paisaje lectura escritura."),
    KC_LNG_LANG("pt", "que o a do da de em um uma para com por mais se os as ao das dos pelo pela seu sua como entre muito depois mundo olá bom dia amigos todos este projeto compara texto corto rápido raposa marrom salta sobre cão. a língua portuguesa é maravilhosa. como você está hoje? espero que tudo esteja bem com você e sua família. programação software computador arquivo pasta aplicativo atualização baixar instalar abrir fechar deletar criar pesquisar executar salvar mover copiar renomear configurar. tela teclado mouse rede conexão sistema usuário senha memória armazenamento diretório. escola hospital música esporte futebol basquete livro jornal televisão telefone internet montanha mar rio floresta cidade vila rua prédio casa carro avião trem navio."),
    KC_LNG_LANG("fr", "le la les de des un une et est dans que qui pour pas plus ce sur avec au par se sont nous vous son sa ses monde bonjour tous ce projet compare texte court le renard brun saute par dessus chien paresseux. comment allez-vous? la france est un pays magnifique. la liberté est un droit fondamental pour chaque être humain. programmation logiciel ordinateur fichier dossier application mise à jour télécharger installer ouvrir fermer supprimer créer rechercher exécuter sauvegarder déplacer copier renommer configurer. écran clavier souris réseau connexion système utilisateur mot de passe mémoire stockage répertoire. école hôpital musique sport football basketball livre journal télévision téléphone internet montagne mer rivière forêt ville village rue bâtiment maison voiture avion train bateau."),
    KC_LNG_LANG("it", "il la lo i le gli un una e di che in per con si sono ma come nel della delle questo quello non piu mondo ciao buongiorno a tutti questo progetto confronta testi brevi. l'italia è un paese stupendo. spero che questa giornata sia fantastica per te. la pasta e la pizza sono famose in tutto il mondo. programmazione software computer file cartella applicazione aggiornamento scaricare installare aprire chiudere eliminare creare cercare eseguire salvare spostare copiare rinominare configurare. schermo tastiera mouse rete connessione sistema utente password memoria archiviazione directory. scuola ospedale musica sport calcio basket libro giornale televisione telefono internet montagna mare fiume foresta città villaggio strada edificio casa macchina aereo treno nave."),
    KC_LNG_LANG("de", "der die das und ein eine am im in zu von mit für auf den dem nicht ist auch sich als nach vor bei durch welt hallo guten morgen alle dieses projekt vergleicht kurzen text der schnelle braune fuchs springt über den hund. wie geht es dir? deutschland ist bekannt für technik. alles hat ein ende, nur die wurst hat zwei. programmierung software computer datei ordner anwendung aktualisierung herunterladen installieren öffnen schließen löschen erstellen suchen ausführen speichern verschieben kopieren umbenennen konfigurieren. bildschirm tastatur maus netzwerk verbindung system benutzer passwort speicher verzeichnis. schule krankenhaus musik sport fußball basketball buch zeitung fernsehen telefon internet berg meer fluss wald stadt dorf straße gebäude haus auto flugzeug zug schiff."),
    KC_LNG_LANG("nl", "de het een en in is op met voor als aan door naar om over uit bij voor zijn was maar niet uit over door overal. hoe gaat het vandaag met u? nederland is een land van water en molens. deze software werkt heel goed. programmering software computer bestand map applicatie bijwerken downloaden installeren openen sluiten verwijderen maken zoeken uitvoeren opslaan verplaatsen kopiëren hernoemen configureren. scherm toetsenbord muis netwerk verbinding systeem gebruiker wachtwoord geheugen opslag map. school ziekenhuis muziek sport voetbal basketbal boek krant televisie telefoon internet berg zee rivier bos stad dorp straat gebouw huis auto vliegtuig trein schip."),
    KC_LNG_LANG("sv", "och i som att en ett med för på av till den de gör om har han hon det vid från skulle kunna vara alla. hur mår du idag? sverige är ett vackert land i norden. vi gillar att fika och njuta av naturen. programmering mjukvara dator fil mapp applikation uppdatera ladda ner installera öppna stänga ta bort skapa söka köra spara flytta kopiera byta namn konfigurera. skärm tangentbord mus nätverk anslutning system användare lösenord minne lagring katalog. skola sjukhus musik sport fotboll basket bok tidning television telefon internet berg hav flod skog stad by gata byggnad hus bil flygplan tåg fartyg."),
    KC_LNG_LANG("da", "og i det at en til af for med på de som vi er han har ikke ved om fra men de da over efter her. hvad hedder du? danmark er et dejligt land. vi elsker at cykle på de flade veje. hvordan går det med dig? programmering software computer fil mappe applikation opdatering downloade installere åbne lukke slette oprette søge udføre gemme flytte kopiere omdøbe konfigurere. skærm tastatur mus netværk forbindelse system bruger adgangskode hukommelse lager katalog. skole hospital musik sport fodbold basketball bog avis television telefon internet bjerg hav flod skov by landsby gade bygning hus bil fly tog skib."),
    KC_LNG_LANG("no", "og i det at en til av for med på de som vi er han har ikke ved om fra men de da over etter her. norge er et land med mange fjorder og fjell. hvordan har du det i dag? vi er stolte av vår natur. programmering programvare datamaskin fil mappe applikasjon oppdatering laste ned installere åpne lukke slette opprette søke kjøre lagre flytte kopiere gi nytt navn konfigurere. skjerm tastatur mus nettverk tilkobling system bruker passord minne lagring katalog. skole sykehus musikk sport fotball basketball bok avis fjernsyn telefon internett fjell hav elv skog by landsby gate bygning hus bil fly tog skip."),
    KC_LNG_LANG("pl", "w i na do że z o na za przy od po do by się jako który nie ale tak jak dla nich. cześć jak się masz? polska to kraj o bogatej historii. lubię jeść pierogi i spacerować po starym mieście. programowanie oprogramowanie komputer plik folder aplikacja aktualizacja pobierz zainstaluj otwórz zamknij usuń utwórz wyszukaj uruchom zapisz przenieś skopiuj zmień nazwę skonfiguruj. ekran klawiatura mysz sieć połączenie system użytkownik hasło pamięć magazyn katalog. szkoła szpital muzyka sport piłka nożna koszykówka książka gazeta telewizja telefon internet góra morze rzeka las miasto wieś ulica budynek dom samochód samolot pociąg statek."),
    KC_LNG_LANG("tr", "ve bir bu da de için çok ama en daha her kadar gibi olan olanlar ancak değil mi için mi bu ne o zaman. nasılsın kardeşim? türkiye çok güzel bir ülke. istanbul kıtalar arası bir köprüdür. programlama yazılım bilgisayar dosya klasör uygulama güncelleme indirmek yüklemek açmak kapatmak silmek oluşturmak aramak çalıştırmak kaydetmek taşımak kopyalamak yeniden adlandırmak yapılandırmak. ekran klavye fare ağ bağlantı sistem kullanıcı şifre bellek depolama dizin. okul hastane müzik spor futbol basketbol kitap gazete televizyon telefon internet dağ deniz nehir orman şehir köy sokak cadde bina ev araba uçak tren gemi."),
    KC_LNG_LANG("id", "dan yang di ke untuk ada adalah dengan dan itu ini saya kamu dia mereka kami kita dapat dalam dari pada. apa kabar hari ini teman-teman? indonesia adalah negara kepulauan yang sangat luas. saya suka makan nasi goreng. pemrograman perangkat lunak komputer berkas folder aplikasi pembaruan unduh pasang buka tutup hapus buat cari jalankan simpan pindah salin ganti nama atur. layar papan ketik mouse jaringan koneksi sistem pengguna kata sandi memori penyimpanan direktori. sekolah rumah sakit musik olahraga sepak bola basket buku koran televisi telepon internet gunung laut sungai hutan kota desa jalan bangunan rumah mobil pesawat kereta kapal."),
    KC_LNG_LANG("ro", "și în o de la care pe un pentru că să se a fi cu din s-au fost mai au prin pre ea el ei. ce mai faci? românia este o țară frumoasă situată în europa de est. mămăliga este un preparat tradițional foarte gustos. programare software calculator fișier folder aplicație actualizare descărcare instalare deschidere închidere ștergere creare căutare executare salvare mutare copiere redenumire configurare. ecran tastatură mouse rețea conexiune sistem utilizator parolă memorie stocare director. școală spital muzică sport fotbal baschet carte ziar televizor telefon internet munte mare râu pădure oraș sat stradă clădire casă mașină avion tren navă."),
    KC_LNG_LANG("cs", "a že v na s do za pro k s o po u by jako který on ona oni ono jak se mi tebe. ahoj jak se máš? česká republika je známá svým pivem a hrady. praha je srdce evropy. programování software počítač soubor složka aplikace aktualizace stáhnout nainstalovat otevřít zavřít smazat vytvořit hledat spustit uložit přesunout zkopírovat přejmenovat nakonfigurovat. obrazovka klávesnice myš síť připojení systém uživatel heslo paměť úložiště adresář. škola nemocnice hudba sport fotbal basketbal kniha noviny televize telefon internet hora moře řeka les město vesnice ulice budova dům auto letadlo vlak loď."),
    KC_LNG_LANG("hu", "a az és egy hogy van volt lesz neki nem ő de mint is ha már csak még el ki le be fel át. hogy vagy barátom? magyarország a gulyás és a paprika földje. budapest gyönyörű város a duna partján. programozás szoftver számítógép fájl mappa alkalmazás frissítés letöltés telepítés megnyitás bezárás törlés létrehozás keresés futtatás mentés áthelyezés másolás átnevezés konfigurálás. képernyő billentyűzet egér hálózat kapcsolat rendszer felhasználó jelszó memória tárolás könyvtár. iskola kórház zene sport foci kosárlabda könyv újság televízió telefon internet hegy tenger folyó erdő város falu utca épület ház autó repülő vonat hajó."),
    KC_LNG_LANG("fi", "ja se on että hänet hän he heidän meidän teidän olla oli on se että ei mutta niitä. mitä kuuluu? suomi on tuhansien järvien maa. revontulet ovat upeita talvella. ohjelmointi ohjelmistokehitys tietokone tiedosto kansio sovellus päivittää ladata asentaa avata sulkea poistaa luoda etsiä suorittaa tallentaa siirtää kopioida nimetä uudelleen määrittää. näyttö näppäimistö hiiri verkko yhteys laite järjestelmä käyttäjä salasana muisti tallennus hakemisto. koulu sairaala musiikki urheilu jalkapallo koripallo kirja lehti televisio puhelin internet vuori meri joki metsä kaupunki kylä katu rakennus talo auto lentokone juna laiva."),
    KC_LNG_LANG("ru", "и в во не на я что тот быть с а весь по он она они это как но так за из о от около привет как дела? доброе утро всем. русский язык сложный но интересный. программирование программа компьютер файл папка приложение обновление скачать установить открыть закрыть удалить создать найти запустить сохранить переместить копировать переименовать настроить. экран клавиатура мышь сеть соединение система пользователь пароль память хранилище каталог. школа больница музыка спорт футбол баскетбол книга газета телевизор телефон интернет гора море река лес город деревня улица здание дом машина самолёт поезд корабль."),
    KC_LNG_LANG("uk", "і в на що та як він це не було за до для від про але було при. як справи? україна це вільна та незалежна країна. слава україні! програмування програма комп'ютер файл папка додаток оновлення завантажити встановити відкрити закрити видалити створити знайти запустити зберегти перемістити копіювати перейменувати налаштувати. екран клавіатура миша мережа з'єднання система користувач пароль пам'ять сховище каталог. школа лікарня музика спорт футбол баскетбол книга газета телевізор телефон інтернет гора море річка ліс місто село вулиця будівля будинок машина літак поїзд корабель."),
    KC_LNG_LANG("bg", "и в на че да са за той като се по от му си със бил здравейте как сте? българия е стара страна в европа. морето е красиво и планините са величествени. българският език има богата история. програмиране програма компютър файл папка приложение актуализация изтегляне инсталиране отваряне затваряне изтриване създаване намиране стартиране запазване преместване копиране преименуване конфигуриране. екран клавиатура мишка мрежа връзка система потребител парола памет съхранение директория. училище болница музика спорт футбол баскетбол книга вестник телевизия телефон интернет планина море река гора град село улица сграда къща кола самолет влак кораб."),
    KC_LNG_LANG("el", "και το να είναι στο με για του ότι δεν θα από με τα οι που την ο στην από. γεια σας τι κάνετε; η ελλάδα είναι η χώρα του φωτός και της δημοκρατίας. καλή σου μέρα. γιατροί νοσοκομείο θεραπεία ασθενείς δικαστήριο νόμος πολίτες έρευνα επιστήμη πανεπιστήμιο περιβάλλον οικονομία ενέργεια σχολείο κοινότητα πόλη θάλασσα βουνό ιστορία μουσική βιβλίο οικογένεια εργασία."),
    KC_LNG_LANG("ar", "من في على أن إلى ما لا عن مع كان هو الذي التي هذا هذه كل بعد إذا كان. مرحبا بك يا صديقي كيف حالك؟ اللغة العربية لغة الضاد وهي لغة تاريخية عريقة. مستشفى أطباء علاج مرضى قانون محكمة مواطنون بحث علم جامعة بيئة اقتصاد طاقة مدرسة مجتمع مدينة بحر جبل تاريخ موسيقى كتاب أسرة عمل."),
    KC_LNG_LANG("he", "את של על כי המה עם כל גם את זה פה אבל לא אם הוא היא הם אלו. מה שלומך היום? ישראל היא מדינה קטנה עם היסטוריה גדולה. רופאים בית חולים טיפול מטופלים חוק משפט אזרחים מחקר מדע אוניברסיטה סביבה כלכלה אנרגיה בית ספר קהילה עיר ים הר היסטוריה מוזיקה ספר משפחה עבודה."),
    KC_LNG_LANG("hi", "और के में है कि को ही से का पर भी यह तो था वह वे जो किया जाता है। नमस्ते क्या हाल है? भारत एक बहुत बड़ा और विविधतापूर्ण देश है। अस्पताल डॉक्टर उपचार मरीज कानून अदालत नागरिक अनुसंधान विज्ञान विश्वविद्यालय पर्यावरण अर्थव्यवस्था ऊर्जा विद्यालय समुदाय शहर समुद्र पहाड़ इतिहास संगीत पुस्तक परिवार काम।"),
    KC_LNG_LANG("ja", "の に は を た で が と し て い れ ば な から まで より も ます です こんにちは。 日本は技術と伝統が共存する素晴らしい国です。 病院 医師 治療 患者 法律 裁判 市民 研究 科学 大学 環境 経済 エネルギー 学校 地域 都市 海 山 歴史 音楽 本 家族 仕事。 医療の現場では医師が患者に予防と治療の方法を説明します。大学の研究者は観測した光と集めたデータを使い科学のモデルを調べます。"),
    KC_LNG_LANG("ko", "안녕하세요 감사합니다 이것은 언어 감지 프로젝트입니다. 한국어는 매우 아름다운 언어입니다. 오늘 기분이 어떠신가요? 병원 의사 치료 환자 법률 재판 시민 연구 과학 대학 환경 경제 에너지 학교 지역 도시 바다 산 역사 음악 책 가족 일. 의료 현장에서는 의사가 환자에게 예방과 치료 방법을 설명합니다. 대학 연구자는 관측한 빛과 수집한 자료를 사용해 과학 모형을 검토합니다."),
    {
        .code = NULL,
        .seed = NULL,
        .profile = {{{0}, 0}},
        .profile_size = 0,
        .total = 0
    }
};

#ifdef _WIN32
static INIT_ONCE kc_lng_once = INIT_ONCE_STATIC_INIT;
#else
static pthread_once_t kc_lng_once = PTHREAD_ONCE_INIT;
#endif

/**
 * Returns the byte length of a UTF-8 sequence from its lead byte.
 * @param c Lead byte of the UTF-8 sequence.
 * @return Number of bytes in the sequence, or 1 for invalid input.
 */
static int kc_lng_u8_len(unsigned char c) {
    if (c < 0x80U) {
        return 1;
    }
    if ((c & 0xE0U) == 0xC0U) {
        return 2;
    }
    if ((c & 0xF0U) == 0xE0U) {
        return 3;
    }
    if ((c & 0xF8U) == 0xF0U) {
        return 4;
    }
    return 1;
}

/**
 * Performs basic UTF-8 lowercase folding for Cyrillic and Greek.
 * @param c1 First byte of the UTF-8 sequence (modified in place).
 * @param c2 Second byte of the UTF-8 sequence (modified in place).
 * @return No return value.
 */
static void kc_lng_u8_lower(unsigned char *c1, unsigned char *c2) {
    if (*c1 == 0xD0U && (*c2 >= 0x90U && *c2 <= 0xAFU)) {
        *c2 = (unsigned char)(*c2 + 0x20U);
    } else if (*c1 == 0xCEU && (*c2 >= 0x91U && *c2 <= 0xABU)) {
        *c2 = (unsigned char)(*c2 + 0x20U);
    }
}

/**
 * Normalizes input text by lowercasing and collapsing whitespace/punctuation.
 * @param input Input text to normalize.
 * @return Allocated string the caller must free, or NULL on error.
 */
static char *kc_lng_normalize(const char *input) {
    size_t i;
    size_t j;
    size_t len;
    int in_space;
    char *out;

    if (input == NULL) {
        return NULL;
    }

    len = strlen(input);
    out = (char *)malloc(len + 1U);
    if (out == NULL) {
        return NULL;
    }

    i = 0U;
    j = 0U;
    in_space = 1;
    while (i < len) {
        unsigned char c1;
        int clen;

        c1 = (unsigned char)input[i];
        clen = kc_lng_u8_len(c1);

        if (clen == 1 && (isspace((int)c1) || ispunct((int)c1))) {
            if (!in_space) {
                out[j++] = ' ';
                in_space = 1;
            }
            i++;
            continue;
        }

        if (clen == 1) {
            if (c1 >= 'A' && c1 <= 'Z') {
                out[j++] = (char)(c1 + ('a' - 'A'));
            } else {
                out[j++] = (char)c1;
            }
        } else if (clen == 2 && i + 1U < len) {
            unsigned char c2;

            c2 = (unsigned char)input[i + 1U];
            kc_lng_u8_lower(&c1, &c2);
            out[j++] = (char)c1;
            out[j++] = (char)c2;
        } else {
            int k;

            for (k = 0; k < clen && i + (size_t)k < len; k++) {
                out[j++] = input[i + (size_t)k];
            }
        }

        in_space = 0;
        i += (size_t)clen;
    }

    if (j > 0U && out[j - 1U] == ' ') {
        j--;
    }

    out[j] = '\0';
    return out;
}

/**
 * Builds an n-gram frequency profile from a language seed string.
 * @param lang Language entry to populate.
 * @return No return value.
 */
static void kc_lng_train(kc_lng_lang_t *lang) {
    char *normalized;
    int len;
    int i;

    if (lang == NULL || lang->profile_size > 0 || lang->seed == NULL) {
        return;
    }

    normalized = kc_lng_normalize(lang->seed);
    if (normalized == NULL) {
        return;
    }

    len = (int)strlen(normalized);
    for (
        i = 0;
        i <= len - KC_LNG_NG_SIZE;
        i += kc_lng_u8_len((unsigned char)normalized[i])
    ) {
        char gram[12];
        const char *it;
        int byte_count;
        int char_count;
        int j;
        int found;

        memset(gram, 0, sizeof(gram));
        it = normalized + i;
        byte_count = 0;
        char_count = 0;

        while (char_count < KC_LNG_NG_SIZE && (it - normalized) < len) {
            int clen;

            clen = kc_lng_u8_len((unsigned char)*it);
            if (byte_count + clen >= (int)sizeof(gram)) {
                break;
            }

            memcpy(gram + byte_count, it, (size_t)clen);
            byte_count += clen;
            it += clen;
            char_count++;
        }

        if (char_count != KC_LNG_NG_SIZE) {
            continue;
        }

        found = 0;
        for (j = 0; j < lang->profile_size; j++) {
            if (strcmp(lang->profile[j].gram, gram) == 0) {
                lang->profile[j].count++;
                found = 1;
                break;
            }
        }

        if (!found && lang->profile_size < KC_LNG_MAX_GRAMS) {
            strcpy(lang->profile[lang->profile_size].gram, gram);
            lang->profile[lang->profile_size].count = 1;
            lang->profile_size++;
        }

        lang->total++;
    }

    free(normalized);
}

/**
 * Computes a normalized match score for text against one language profile.
 * @param text Input text to evaluate.
 * @param lang Language profile to score against.
 * @return Score in [0, 1], or 0.0 when the match ratio is below threshold.
 */
static double kc_lng_score(const char *text, const kc_lng_lang_t *lang) {
    char *normalized;
    int len;
    int i;
    int total_grams;
    int matches;
    double log_sum;

    normalized = kc_lng_normalize(text);
    if (normalized == NULL || lang == NULL || lang->total <= 0L) {
        free(normalized);
        return 0.0;
    }

    len = (int)strlen(normalized);
    total_grams = 0;
    matches = 0;
    log_sum = 0.0;

    for (
        i = 0;
        i <= len - KC_LNG_NG_SIZE;
        i += kc_lng_u8_len((unsigned char)normalized[i])
    ) {
        char gram[12];
        const char *it;
        int byte_count;
        int char_count;
        int j;
        int count;

        memset(gram, 0, sizeof(gram));
        it = normalized + i;
        byte_count = 0;
        char_count = 0;

        while (char_count < KC_LNG_NG_SIZE && (it - normalized) < len) {
            int clen;

            clen = kc_lng_u8_len((unsigned char)*it);
            if (byte_count + clen >= (int)sizeof(gram)) {
                break;
            }

            memcpy(gram + byte_count, it, (size_t)clen);
            byte_count += clen;
            it += clen;
            char_count++;
        }

        if (char_count != KC_LNG_NG_SIZE) {
            continue;
        }

        count = 0;
        for (j = 0; j < lang->profile_size; j++) {
            if (strcmp(lang->profile[j].gram, gram) == 0) {
                count = lang->profile[j].count;
                matches++;
                break;
            }
        }

        log_sum += log(
            ((double)count + 0.1) /
            ((double)lang->total + ((double)lang->profile_size * 0.1))
        );
        total_grams++;
    }

    free(normalized);

    if (total_grams == 0 || ((double)matches / (double)total_grams) < 0.01) {
        return 0.0;
    }

    double avg_log_prob = log_sum / (double)total_grams;

    return 1.0 / (
        1.0 + exp(-4.0 * (avg_log_prob - (-7.0)))
    );
}

/**
 * Compares two ranked results by descending score for qsort.
 * @param left Pointer to the first kc_lng_rank_t.
 * @param right Pointer to the second kc_lng_rank_t.
 * @return Negative, zero, or positive ordering value.
 */
static int kc_lng_rank_cmp(const void *left, const void *right) {
    const kc_lng_rank_t *a;
    const kc_lng_rank_t *b;

    a = (const kc_lng_rank_t *)left;
    b = (const kc_lng_rank_t *)right;

    if (b->score > a->score) {
        return 1;
    }
    if (b->score < a->score) {
        return -1;
    }
    return 0;
}

/**
 * Trains all language profiles exactly once.
 * @return No return value.
 */
static void kc_lng_init_once(void) {
    int i;

    for (i = 0; i < KC_LNG_MAX_LANGS && kc_lng_langs[i].code != NULL; i++) {
        kc_lng_train(&kc_lng_langs[i]);
    }
}

#ifdef _WIN32
/**
 * Windows one-time init trampoline for InitOnceExecuteOnce.
 * @param once One-time initialization object (unused).
 * @param param Unused callback parameter.
 * @param context Unused callback context.
 * @return TRUE on success.
 */
static BOOL CALLBACK kc_lng_init_once_win(
    PINIT_ONCE once,
    PVOID param,
    PVOID *context
) {
    (void)once;
    (void)param;
    (void)context;
    kc_lng_init_once();
    return TRUE;
}
#endif

/**
 * Ensures internal language profiles are initialized via once-control.
 * @return KC_LNG_OK on success, KC_LNG_ERROR on failure.
 */
static int kc_lng_ensure_initialized(void) {
#ifdef _WIN32
    if (InitOnceExecuteOnce(&kc_lng_once, kc_lng_init_once_win, NULL, NULL)) {
        return KC_LNG_OK;
    }
    return KC_LNG_ERROR;
#else
    if (pthread_once(&kc_lng_once, kc_lng_init_once) != 0) {
        return KC_LNG_ERROR;
    }
    return KC_LNG_OK;
#endif
}

/**
 * Detect languages for input text.
 * Summary: Detect languages for input text.
 *
 * @param text Input text.
 * @param threshold Minimum score threshold.
 * @param limit Maximum number of results.
 * @param out_results Output array (caller-owned).
 * @param out_count Output count.
 * @return KC_LNG_OK on success, KC_LNG_ERROR on failure.
 */
int kc_lng_detect(const char *text, double threshold, size_t limit, kc_lng_result_t **out_results, size_t *out_count) {
    kc_lng_rank_t ranks[KC_LNG_MAX_LANGS];
    size_t lang_count;
    size_t filtered_count;
    size_t i;
    int idx;

    if (out_results) {
        *out_results = NULL;
    }
    if (out_count) {
        *out_count = 0;
    }

    if (text == NULL || out_results == NULL || out_count == NULL || limit == 0 || !isfinite(threshold) || threshold < 0.0 || threshold > 1.0) {
        return KC_LNG_ERROR;
    }

    if (limit > KC_LNG_MAX_LANGS) {
        limit = KC_LNG_MAX_LANGS;
    }

    if (kc_lng_ensure_initialized() != KC_LNG_OK) {
        return KC_LNG_ERROR;
    }

    lang_count = 0;
    for (idx = 0; idx < KC_LNG_MAX_LANGS && kc_lng_langs[idx].code != NULL; idx++) {
        ranks[lang_count].code = kc_lng_langs[idx].code;
        ranks[lang_count].score = kc_lng_score(text, &kc_lng_langs[idx]);
        lang_count++;
    }

    qsort(ranks, lang_count, sizeof(ranks[0]), kc_lng_rank_cmp);

    filtered_count = 0;
    for (i = 0; i < lang_count && filtered_count < limit; i++) {
        if (ranks[i].score >= threshold) {
            filtered_count++;
        }
    }

    if (filtered_count == 0) {
        return KC_LNG_OK;
    }

    {
        kc_lng_result_t *arr = (kc_lng_result_t *)malloc(filtered_count * sizeof(*arr));
        size_t written = 0;
        if (arr == NULL) {
            return KC_LNG_ERROR;
        }
        for (i = 0; i < lang_count && written < filtered_count; i++) {
            if (ranks[i].score >= threshold) {
                arr[written].code = ranks[i].code;
                arr[written].score = ranks[i].score;
                written++;
            }
        }
        *out_results = arr;
        *out_count = filtered_count;
    }

    return KC_LNG_OK;
}

/**
 * Release memory allocated by kc_lng_detect.
 * Summary: Release memory allocated by kc_lng_detect.
 *
 * @param ptr Pointer returned via out_results (NULL safe, no-op on NULL).
 * @return None.
 */
void kc_lng_free(void *ptr) {
    free(ptr);
}

#ifndef KC_LNG_BUILD_VERSION
#define KC_LNG_BUILD_VERSION 0
#endif

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_lng_version(void) {
    return (uint64_t)KC_LNG_BUILD_VERSION;
}
