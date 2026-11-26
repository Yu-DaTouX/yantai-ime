"""Reproducible cleaned & enriched rime-frost product. Standard library; never reads installed user data.

The source lock pins every package, supplemental dictionary, supporting database and the canonical output.
No floating refs, SCEL parsing, automatic polyphone expansion or user-journal writes.
SPDX-License-Identifier: GPL-3.0-only
"""
from __future__ import annotations

import collections
from concurrent.futures import ThreadPoolExecutor
from contextlib import closing
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import sqlite3
import tempfile
import unicodedata

from product_lock_shared import download_with_retries, sha256
from . import symbol_patch

ROOT = Path(__file__).resolve().parents[2]
LOCK = Path(__file__).with_name("frost.lock.json")
OVERLAY = Path(__file__).with_name("frost_overlay.json")
OUT = ROOT / "MetasequoiaImeDict/out"
CACHE = ROOT / "MetasequoiaImeDict/cache/frost"
_spec = importlib.util.spec_from_file_location("frost_format", ROOT / "engine/contracts/dictionary/format.py")
FORMAT = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(FORMAT)
_spec = importlib.util.spec_from_file_location("frost_product", ROOT / "scripts/dictionary_product.py")
PRODUCT = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(PRODUCT)

# 与 engine/quanpin/quanpin_utils.cpp 的 canonical_umlaut_spelling() 保持一致：
# j/q/x/y 系写 u，l/n 系的 lue/nue 写 v（lve/nve）。
CANONICAL_SYLLABLE_MAP = {
    "lue": "lve", "nue": "nve",
    "jv": "ju", "jvan": "juan", "jve": "jue", "jvn": "jun",
    "qv": "qu", "qvan": "quan", "qve": "que", "qvn": "qun",
    "xv": "xu", "xvan": "xuan", "xve": "xue", "xvn": "xun",
    "yv": "yu", "yvan": "yuan", "yve": "yue", "yvn": "yun",
}

# 与 engine/quanpin/quanpin_utils.cpp::intact_pinyin_list() 规范化集合一致，另含合法普通话音节 tei/eng/rua。
# 自动拦截 GB18030-2022 的 15,764 条虚拟音节 ziang 及 sei/lvan/yai/sua 等非法音节。
VALID_SYLLABLES = frozenset({
    "a", "ai", "an", "ang", "ao",
    "ba", "bai", "ban", "bang", "bao", "bei", "ben", "beng", "bi", "bian", "biang", "biao", "bie", "bin", "bing", "bo", "bu",
    "ca", "cai", "can", "cang", "cao", "ce", "cen", "ceng", "cha", "chai", "chan", "chang", "chao", "che", "chen", "cheng", "chi", "chong", "chou", "chu", "chua", "chuai", "chuan", "chuang", "chui", "chun", "chuo", "ci", "cong", "cou", "cu", "cuan", "cui", "cun", "cuo",
    "da", "dai", "dan", "dang", "dao", "de", "dei", "den", "deng", "di", "dia", "dian", "diao", "die", "ding", "diu", "dong", "dou", "du", "duan", "dui", "dun", "duo",
    "e", "ei", "en", "eng", "er",
    "fa", "fan", "fang", "fei", "fen", "feng", "fiao", "fo", "fou", "fu",
    "ga", "gai", "gan", "gang", "gao", "ge", "gei", "gen", "geng", "gong", "gou", "gu", "gua", "guai", "guan", "guang", "gui", "gun", "guo",
    "ha", "hai", "han", "hang", "hao", "he", "hei", "hen", "heng", "hong", "hou", "hu", "hua", "huai", "huan", "huang", "hui", "hun", "huo",
    "ji", "jia", "jian", "jiang", "jiao", "jie", "jin", "jing", "jiong", "jiu", "ju", "juan", "jue", "jun",
    "ka", "kai", "kan", "kang", "kao", "ke", "kei", "ken", "keng", "kong", "kou", "ku", "kua", "kuai", "kuan", "kuang", "kui", "kun", "kuo",
    "la", "lai", "lan", "lang", "lao", "le", "lei", "leng", "li", "lia", "lian", "liang", "liao", "lie", "lin", "ling", "liu", "lo", "long", "lou", "lu", "luan", "lun", "luo", "lv", "lve",
    "ma", "mai", "man", "mang", "mao", "me", "mei", "men", "meng", "mi", "mian", "miao", "mie", "min", "ming", "miu", "mo", "mou", "mu",
    "na", "nai", "nan", "nang", "nao", "ne", "nei", "nen", "neng", "ni", "nian", "niang", "niao", "nie", "nin", "ning", "niu", "nong", "nou", "nu", "nuan", "nun", "nuo", "nv", "nve",
    "o", "ou",
    "pa", "pai", "pan", "pang", "pao", "pei", "pen", "peng", "pi", "pian", "piao", "pie", "pin", "ping", "po", "pou", "pu",
    "qi", "qia", "qian", "qiang", "qiao", "qie", "qin", "qing", "qiong", "qiu", "qu", "quan", "que", "qun",
    "ran", "rang", "rao", "re", "ren", "reng", "ri", "rong", "rou", "ru", "rua", "ruan", "rui", "run", "ruo",
    "sa", "sai", "san", "sang", "sao", "se", "sen", "seng", "sha", "shai", "shan", "shang", "shao", "she", "shei", "shen", "sheng", "shi", "shou", "shu", "shua", "shuai", "shuan", "shuang", "shui", "shun", "shuo", "si", "song", "sou", "su", "suan", "sui", "sun", "suo",
    "ta", "tai", "tan", "tang", "tao", "te", "tei", "teng", "ti", "tian", "tiao", "tie", "ting", "tong", "tou", "tu", "tuan", "tui", "tun", "tuo",
    "wa", "wai", "wan", "wang", "wei", "wen", "weng", "wo", "wu",
    "xi", "xia", "xian", "xiang", "xiao", "xie", "xin", "xing", "xiong", "xiu", "xu", "xuan", "xue", "xun",
    "ya", "yan", "yang", "yao", "ye", "yi", "yin", "ying", "yo", "yong", "you", "yu", "yuan", "yue", "yun",
    "za", "zai", "zan", "zang", "zao", "ze", "zei", "zen", "zeng", "zha", "zhai", "zhan", "zhang", "zhao", "zhe", "zhei", "zhen", "zheng", "zhi", "zhong", "zhou", "zhu", "zhua", "zhuai", "zhuan", "zhuang", "zhui", "zhun", "zhuo", "zi", "zong", "zou", "zu", "zuan", "zui", "zun", "zuo",
})

# 仅作用于声明了 drop_traditional_multi_char 的细胞包：不含任何 GB2312 简体字的明确繁体字集。
_TRAD_CANDIDATES = (
    "萬與專業叢東絲兩嚴喪個豐臨為麗舉麼義烏樂喬習鄉書買亂虧雲亞產親億僅從倉儀們價眾優夥會傘偉傳傷倫偽體餘僑儉債傾僕償儲黨蘭關興養獸冊寫軍農決況凍幾鳳憑凱擊鑿劃別刪劑創劇劉劍勁勞勢務勝動勸匯匱區醫華協單賣衛卻廠廳歷厲壓厭廁廂廈廚變敘葉號嘆聽啟員嗚啞嘩響喚單噴嘯囑國圖團園圍圓聖場塊堅執報塑塗塚塵境墊墜增墮壁壞壟壯壽夠夢夾奪奮獎奧婦媽娛嬰學孫寧實審寬寶寵將專尊對導尋爾嘗層屢屬崗島嶺嶽巒幣師帳帶幀幅幟幫廣莊慶廬庫應龐廢廟開異棄張強彈歸當錄彙徹徵德憶憂憲懷懸戀戲戰戶拋護擔據擠擬擴攝擺擾攀攔攜攤敗敵數整斂斃斷無晝暈暉暢暫曆曉曠書朧機殺雜權條來楊傑極構槍楓樞標樓樹橋橫檢檔櫃欄歡歐歲歸殘毀毅氣漢湯溝溫滄滅滌滬滾滿漁漆漏演漠漫漲漸潔潘潛潤潮潰澄澆澈澤澳激濃濁濕濟濤濫濱瀉瀋濾瀆灘灣災點煩熱燒燙營爍爐爛牆犢犧狹獨獅獄獻獲獵環璽瓊產畫疇疊療發監盤盧盪盡睜瞞矚矯研磚碼確礙礦礪礫礎視禦禪禮禱離種稱穀積穎穩穫窮窯竄竊競筆築籌簽簡簫簷籃簿籍籤類糧糰緊紅約級紀緯純紙紛紡紐線練組細織終絆紹經結繞繪給絢絕絞絡統絲絳絹綁綏綜綠維綱網綴綵綸綺綻綽綾綿緋緒緘緝緞緩締縷編緣縛縫縮縱總績繁繃繆繅繕繡繩繫繭繳繼纖續罰罷羅羈羆羨群聖聞聯聰聲職聽肅脅脈腳脫臉臘臟騰輿艦艱艷藝節範薦薩藥蘇蘋處虛號虧蟲蝕補裝裏製複褲襪襲見觀規覓視覽覺譽計訂訃認討讓訖訓議訊記講諱謳詎訝許訛論訟諷設訪訣證評詛識詐訴診詆詞譯試詩詰詼誠誅話誕詬詮詭詢詣諍該詳詫詡誡誣語誤誥誘誨說誦請諸諾讀誹課諉諛誰諗調諂諒諄談誼謀諜謊諫諧謁謂諤諭諮諳諺諦謎謝謠謗謙謐謹謾謫謬譚譜譎讚豐豔豬貓貝貞負財貢貧貨販貪貫責貯貴貶買貸費貼貽貿賀賂賃賄資賈賊賑賒賓賜賞賠賢賣賤賦質賬賭賴賺購賽贄贅贈贊贍贏贓贖贗贛趙趕趨跡踐踴躊躋躍車軋軌軍軒軛軟軫軸軻軼輕軾較載輒輓輔輛輝輟輥輩輪輯輸輻輾輿轄轅轆轉轍轎轟轡辦辭辯農迅迎運近返還這進遠違連遲適選遜遞遺遙鄧鄭鄰鬱釀釁釋釐金針釣鈔鈕鈞鈴鈷錢鉗鉅鑽鐵鉑鉛鉚鐸銅鋁鎧銑銘鉸鉻銀銷銹銻銼鋅鋒鋤鋪銳鋰鋸鋼錄錐錘錠錦錨錫錮錯錳錶鍵鍾鎂鎊鎔鎖鎘鎚鎳鎢鎬鎮鏈鏡鏢鏤鏽鐘鑄鑑鑒鑰鑲鑷鑼長門閃閉開閏閑間閔閘閡閣閥閨閩閱閹閻闇闊闌闔闕闖闡闢陰陳陸陽隊階際障礙隱隴隸隻雋雖雙雛雜雞離難電霧靈靜韌韓韋音韻頁頂頃項順須頌預頑頒頓頗領頜頭頰頷頸頹頻顆題額顎顏願顛類顧顫顯顰顱顴風颯颶飄飆飛飢飲飯飼飽飾餃餅餉養餌餐餒餓餘餚餛餞餡館餵餾饅饋饒饗饞馬馭馮馱馳馴駁驢駛駟駙駒駐駝駕驛驍罵驕駱駭駢騁驗駿騏騎騷驚驟驤驥體髮鬆鬍鬚鬥鬧鬱魚魯鮑鮮鯉鯊鯨鰓鱗鳥鳩雞鳶鳴鷗鴉鴨鴦鴛鴕鴻鴿鵑鵝鵠鵡鵪鵬鵰鵲鶉鶯鶴鶻鷗鷹鷺鸚鸛鸝鸞鹵鹹鹼鹽麥麩麴麵黃點默黷黿鼉齊齋齒齡齣齦齟齬齲齷龍龐龔龕龜"
)


def _build_strict_trad_set() -> frozenset[str]:
    result = set()
    for ch in _TRAD_CANDIDATES:
        try:
            ch.encode("gb2312")
        except UnicodeEncodeError:
            result.add(ch)
    return frozenset(result)


STRICT_TRAD_CHARS = _build_strict_trad_set()


def has_strict_traditional(word: str) -> bool:
    return any(ch in STRICT_TRAD_CHARS for ch in word)


def write_json(path: Path, value) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


def recipe() -> dict:
    value = json.loads(LOCK.read_text(encoding="utf-8"))
    if (value.get("schema_version") != 1 or value.get("repository") != "gaboolic/rime-frost" or
            not re.fullmatch("[0-9a-f]{40}", value.get("commit", "")) or value.get("profile") != "full" or
            value.get("format_version") != FORMAT.FORMAT["formatVersion"] or value.get("weight_scale") != 5):
        raise ValueError("Unsupported frost recipe")
    seen = set()
    for entry in value["sources"]:
        path = PurePosixPath(entry["path"])
        if (path.is_absolute() or ".." in path.parts or not re.fullmatch(r"[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*", entry["path"]) or entry["path"] in seen):
            raise ValueError("Invalid/duplicate frost source path")
        seen.add(entry["path"])
        if (not re.fullmatch("[0-9a-f]{64}", entry["sha256"]) or
                type(entry["size"]) is not int or entry["size"] < 0 or entry["role"] not in ("dictionary", "notice")):
            raise ValueError("Invalid frost source metadata")
    seen_supps = set()
    for supp in value.get("supplements", []):
        name = supp.get("name", "")
        filename = supp.get("filename", "")
        if (not name or name in seen_supps or not re.fullmatch(r"[A-Za-z0-9_.-]+", filename) or
                not re.fullmatch("[0-9a-f]{64}", supp.get("sha256", "")) or
                type(supp.get("size")) is not int or supp["size"] < 0 or
                not str(supp.get("url", "")).startswith("https://")):
            raise ValueError(f"Invalid supplement metadata: {name!r}")
        if "commit" in supp and not re.fullmatch("[0-9a-f]{40}", supp["commit"]):
            raise ValueError(f"Supplement commit must be a 40-char SHA: {name!r}")
        seen_supps.add(name)
    if set(value["support"]["files"]) != {"english.db", "others.db"}:
        raise ValueError("Only non-pinyin legacy resources may be reused")
    return value


def load_overlay(path: Path = OVERLAY) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema_version") != 1:
        raise ValueError("Unsupported frost overlay schema_version")
    return data


def builder_digest() -> str:
    digest = hashlib.sha256()
    paths = [Path(__file__), LOCK, OVERLAY, ROOT / "scripts/build-dictionary.py",
             ROOT / "engine/contracts/dictionary/format.py", ROOT / "engine/contracts/dictionary/format.json",
             ROOT / "docs/dictionary-frost-sources.md", symbol_patch.LOCK, symbol_patch.EXTRA,
             Path(symbol_patch.__file__)]
    for path in paths:
        digest.update(path.name.encode() + b"\0" + path.read_bytes().replace(b"\r\n", b"\n") + b"\0")
    return digest.hexdigest()


def ensure(path: Path, url: str, digest: str, size: int | None = None) -> Path:
    def valid():
        return path.is_file() and (size is None or path.stat().st_size == size) and sha256(path) == digest
    if not valid():
        path.parent.mkdir(parents=True, exist_ok=True)
        download_with_retries(url, path, attempts=3, timeout=90)
        if not valid():
            path.unlink(missing_ok=True)
            raise ValueError(f"Input does not match reviewed SHA256: {url}")
    return path


def acquire(cache: Path, config: dict) -> Path:
    source = cache / config["commit"]
    def fetch(entry):
        relative = entry["path"]
        url = f"https://raw.githubusercontent.com/{config['repository']}/{config['commit']}/{relative}"
        ensure(source / relative, url, entry["sha256"], entry["size"])
    with ThreadPoolExecutor(max_workers=4) as workers:
        list(workers.map(fetch, config["sources"]))
    for supp in config.get("supplements", []):
        ensure(cache / "supplements" / supp["filename"], supp["url"], supp["sha256"], supp["size"])
    for name, digest in config["support"]["files"].items():
        support = config["support"]
        url = f"https://github.com/{support['repository']}/releases/download/{support['tag']}/{name}"
        ensure(cache / "support" / name, url, digest)
    return source


def han(text: str) -> bool:
    return bool(text) and all(0x3400 <= ord(ch) <= 0x4DBF or 0x4E00 <= ord(ch) <= 0x9FFF or
                             0xF900 <= ord(ch) <= 0xFAFF or 0x20000 <= ord(ch) <= 0x3FFFF or
                             ord(ch) == 0x3007 for ch in text)


def normalize(code: str) -> list[str]:
    code = code.lower().replace("u:", "v").replace("ü", "v")
    for ch in "ǖǘǚǜ":
        code = code.replace(ch, "v")
    code = "".join(ch for ch in unicodedata.normalize("NFD", code) if not unicodedata.combining(ch))
    parts = re.sub(r"[1-5]", "", code).replace("'", " ").split()
    return [CANONICAL_SYLLABLE_MAP.get(part, part) for part in parts]


def _read_dict_text(path: Path) -> str:
    if path.suffix == ".gz":
        with gzip.open(path, "rt", encoding="utf-8-sig") as stream:
            return stream.read()
    return path.read_text(encoding="utf-8-sig")


def parse_package(path: Path, *, skip_sections: tuple[str, ...] | list[str] = (),
                  max_zero_weight_length: int | None = None,
                  drop_traditional_multi_char: bool = False):
    stats, rows, rejects = collections.Counter(), [], []
    skip_section_set = set(skip_sections)
    current_section = ""
    active = False
    for line_number, line in enumerate(_read_dict_text(path).splitlines(), 1):
        stripped = line.strip()
        if stripped == "...":
            active = True
            continue
        if not active or not stripped:
            continue
        if stripped.startswith("#"):
            if stripped.startswith("###"):
                current_section = stripped
            continue
        if current_section in skip_section_set:
            stats["skipped_section_lines"] += 1
            continue
        fields = line.split("\t")
        stats["data_lines"] += 1
        reason = None
        if len(fields) < 2:
            reason = "missing_explicit_code"
        else:
            word = unicodedata.normalize("NFC", fields[0])
            if not han(word):
                reason = "not_pure_han"
            else:
                parts = normalize(fields[1])
                key = "'".join(parts)
                if len(parts) != len(word):
                    reason = "syllable_char_mismatch"
                elif (not parts or any(part not in VALID_SYLLABLES for part in parts) or
                      parts[0][0] not in FORMAT.QUANPIN["initials"]):
                    reason = "invalid_code"
                else:
                    try:
                        weight = int(fields[2]) if len(fields) > 2 and fields[2].strip() else 0
                    except ValueError:
                        reason = "invalid_weight"
                    if reason is None:
                        if weight < 0:
                            reason = "negative_weight"
                        elif (max_zero_weight_length is not None and weight == 0 and
                              len(word) > max_zero_weight_length):
                            reason = "zero_weight_too_long"
                        elif drop_traditional_multi_char and len(word) >= 2 and has_strict_traditional(word):
                            reason = "traditional_multi_char"
                        else:
                            rows.append((key, word, weight))
                            stats["accepted_lines"] += 1
                            stats["zero_weight"] += weight == 0
        if reason:
            stats["rejected_" + reason] += 1
            if len(rejects) < 30:
                rejects.append({"line": line_number, "reason": reason, "text": line})
    if not active:
        raise ValueError(f"Missing Rime dictionary body: {path}")
    return rows, dict(stats), rejects


def _scale_supplement_weight(raw: int, supp: dict) -> int:
    mode = supp.get("weight_mode", "raw")
    w_min = int(supp.get("weight_min", 1))
    w_max = int(supp.get("weight_max", 200))
    if mode == "sqrt_cap":
        scaled = int(raw ** 0.5) if raw > 0 else w_min
    elif mode == "div_cap":
        divisor = max(1, int(supp.get("weight_divisor", 4)))
        scaled = raw // divisor
    else:
        scaled = raw
    return max(w_min, min(w_max, scaled))


def filter_rows(rows: dict, config: dict) -> list[dict]:
    removed = []
    for entry in config.get("filters", []):
        identity = (entry["key"], entry["value"])
        if identity in rows and rows[identity] <= entry["maximum_raw_weight"]:
            removed.append({**entry, "raw_weight": rows.pop(identity)})
    return removed


def prune_char_reading_mismatches(rows: dict, char_readings: dict[str, set[str]],
                                  allowlist: set[tuple[str, str]]) -> list[dict]:
    removed = []
    for key, word in list(rows.keys()):
        if len(word) < 2 or (key, word) in allowlist:
            continue
        parts = key.split("'")
        bad = None
        for ch, syl in zip(word, parts):
            valid = char_readings.get(ch)
            if valid is not None and syl not in valid:
                bad = (ch, syl)
                break
        if bad is not None:
            removed.append({"key": key, "value": word, "raw_weight": rows.pop((key, word)),
                            "bad_char": bad[0], "bad_syllable": bad[1]})
    return removed


def apply_overlay(rows: dict, overlay: dict) -> dict:
    removed = []
    for entry in overlay.get("removals", []):
        identity = (entry["key"], entry["value"])
        max_w = entry.get("maximum_raw_weight")
        if identity in rows and (max_w is None or rows[identity] <= max_w):
            removed.append({**entry, "raw_weight": rows.pop(identity)})
    added = []
    for entry in overlay.get("additions", []):
        key = "'".join(normalize(entry["key"]))
        word = unicodedata.normalize("NFC", entry["value"])
        weight = int(entry["raw_weight"])
        parts = key.split("'")
        if (not han(word) or len(parts) != len(word) or weight < 0 or
                any(p not in VALID_SYLLABLES for p in parts) or
                parts[0][0] not in FORMAT.QUANPIN["initials"]):
            raise ValueError(f"Invalid overlay addition: {entry!r}")
        identity = (key, word)
        previous = rows.get(identity)
        if previous is None or weight > previous:
            rows[identity] = weight
            added.append({"key": key, "value": word, "previous": previous, "raw_weight": weight})
    adjusted = []
    for entry in overlay.get("weight_adjustments", []):
        identity = (entry["key"], entry["value"])
        weight = int(entry["raw_weight"])
        if weight < 0:
            raise ValueError(f"Negative weight adjustment: {entry!r}")
        if identity in rows:
            previous = rows[identity]
            rows[identity] = weight
            adjusted.append({"key": entry["key"], "value": entry["value"],
                             "previous": previous, "raw_weight": weight})
    return {"removed": removed, "added": added, "adjusted": adjusted}


def canonical_digest(connection: sqlite3.Connection) -> str:
    """Byte identity of records, not SQLite's version-dependent page layout."""
    digest = hashlib.sha256()
    for table in FORMAT.quanpin_tables():
        digest.update((table + "\n").encode())
        for row in connection.execute(f'SELECT key,jp,value,weight FROM "{table}" ORDER BY key,value'):
            digest.update((json.dumps(row, ensure_ascii=False, separators=(",", ":")) + "\n").encode())
    return digest.hexdigest()


def build_pinyin(source: Path, output: Path, config: dict, *, filtered: bool = True,
                 supplements_dir: Path | None = None, overlay: dict | None = None) -> dict:
    if output.exists():
        raise ValueError(f"Refusing to overwrite database: {output}")
    entries, packages, rejects = {}, {}, {}
    char_readings: dict[str, set[str]] = collections.defaultdict(set)
    for entry in config["sources"]:
        if entry["role"] != "dictionary":
            continue
        path = source / entry["path"]
        if path.stat().st_size != entry["size"] or sha256(path) != entry["sha256"]:
            raise ValueError(f"Frost source changed: {entry['path']}")
        rows, counts, samples = parse_package(
            path,
            skip_sections=entry.get("skip_sections", ()),
            max_zero_weight_length=entry.get("max_zero_weight_length"),
            drop_traditional_multi_char=bool(entry.get("drop_traditional_multi_char", False)),
        )
        counts.update(new_unique_rows=0, duplicate_rows=0)
        for key, word, weight in rows:
            if len(word) == 1:
                char_readings[word].add(key)
            identity = (key, word)
            if identity in entries:
                counts["duplicate_rows"] += 1
                entries[identity] = max(entries[identity], weight)
            else:
                entries[identity] = weight
                counts["new_unique_rows"] += 1
        packages[entry["path"]] = counts
        rejects[entry["path"]] = samples

    supplement_stats = {}
    if supplements_dir is not None:
        for supp in config.get("supplements", []):
            path = supplements_dir / supp["filename"]
            if path.stat().st_size != supp["size"] or sha256(path) != supp["sha256"]:
                raise ValueError(f"Supplement changed: {supp['name']}")
            rows, counts, samples = parse_package(
                path,
                skip_sections=supp.get("skip_sections", ()),
                max_zero_weight_length=supp.get("max_zero_weight_length"),
                drop_traditional_multi_char=bool(supp.get("drop_traditional_multi_char", False)),
            )
            skip_prefixes = tuple(supp.get("skip_prefixes", ()))
            skip_suffixes = tuple(supp.get("skip_suffixes", ()))
            skip_regex = re.compile(supp["skip_regex"]) if supp.get("skip_regex") else None
            skip_entries = {(e["key"], e["value"]) for e in supp.get("skip_entries", [])}
            insert_mode = supp.get("insert_mode", "missing_or_zero")
            counts.update(new_unique_rows=0, lifted_zero_rows=0, skipped_existing=0, skipped_filtered=0)
            for key, word, raw_w in rows:
                identity = (key, word)
                if (identity in skip_entries or
                        (skip_prefixes and word.startswith(skip_prefixes)) or
                        (skip_suffixes and word.endswith(skip_suffixes)) or
                        (skip_regex is not None and skip_regex.search(word))):
                    counts["skipped_filtered"] += 1
                    continue
                scaled_w = _scale_supplement_weight(raw_w, supp)
                if identity not in entries:
                    entries[identity] = scaled_w
                    counts["new_unique_rows"] += 1
                elif insert_mode == "missing_or_zero" and entries[identity] == 0 and scaled_w > 0:
                    entries[identity] = scaled_w
                    counts["lifted_zero_rows"] += 1
                else:
                    counts["skipped_existing"] += 1
            supplement_stats[supp["name"]] = counts
            rejects[f"supplement:{supp['name']}"] = samples

    removed = filter_rows(entries, config) if filtered else []
    mismatches_removed = []
    if filtered and config.get("validate_char_readings") and char_readings:
        allowlist = {(e["key"], e["value"]) for e in (overlay or {}).get("dialect_reading_allowlist", [])}
        mismatches_removed = prune_char_reading_mismatches(entries, char_readings, allowlist)
    overlay_report = apply_overlay(entries, overlay) if (filtered and overlay is not None) else None

    output.parent.mkdir(parents=True, exist_ok=True)
    with closing(sqlite3.connect(output)) as connection:
        connection.execute("PRAGMA page_size=4096")
        connection.execute("PRAGMA journal_mode=DELETE")
        connection.execute("PRAGMA synchronous=OFF")
        for table in FORMAT.quanpin_tables():
            connection.execute(f'CREATE TABLE "{table}" (key TEXT,jp TEXT,value TEXT,weight INTEGER DEFAULT 0)')
        groups = collections.defaultdict(list)
        for (key, word), raw in entries.items():
            weight = max(1, min(100000000, raw * config["weight_scale"]))
            groups[FORMAT.pinyin_table(key)].append((key, "".join(part[0] for part in key.split("'")), word, weight))
        for table, rows in groups.items():
            connection.executemany(f'INSERT INTO "{table}" VALUES (?,?,?,?)', rows)
        for table in FORMAT.quanpin_tables():
            suffix = table.removeprefix("tbl_")
            connection.execute(f'CREATE INDEX idx_key_{suffix} ON "{table}" (key)')
            connection.execute(f'CREATE INDEX idx_jp_{suffix} ON "{table}" (jp)')
        connection.execute("CREATE TABLE quick_parases (key TEXT NOT NULL,value TEXT NOT NULL,"
                           "weight INTEGER NOT NULL DEFAULT 0,UNIQUE(key,value))")
        connection.execute("CREATE INDEX idx_key_quick_parases ON quick_parases(key)")
        connection.execute("CREATE TABLE latin_words (key TEXT NOT NULL,value TEXT NOT NULL,weight INTEGER NOT NULL,"
                           "exact_only INTEGER NOT NULL DEFAULT 0,PRIMARY KEY(key,value))")
        connection.execute("ANALYZE")
        connection.commit()
        connection.execute("VACUUM")
        if connection.execute("PRAGMA integrity_check").fetchone() != ("ok",):
            raise ValueError("Invalid built pinyin dictionary")
        content_id = canonical_digest(connection)
    return {"rows": len(entries), "packages": packages, "supplements": supplement_stats,
            "removed_fragments": removed, "char_reading_mismatches_removed": len(mismatches_removed),
            "overlay": ({k: len(v) for k, v in overlay_report.items()} if overlay_report else None),
            "reject_samples": rejects, "weight_rule": "max(1,min(100000000,raw_weight*5))",
            "canonical_sha256": content_id, "sha256": sha256(output), "bytes": output.stat().st_size}


def verify(directory: Path, config: dict | None = None) -> dict:
    config = config or recipe()
    manifest = PRODUCT.verify_product(directory, "desktop")
    if manifest.get("source") != {"repository": config["repository"], "commit": config["commit"], "dirty": False}:
        raise ValueError("Wrong frost product provenance")
    if manifest.get("recipe_sha256") != sha256(LOCK) or manifest.get("builder_sha256") != builder_digest():
        raise ValueError("Frost recipe/builder changed; rebuild required")
    notices = {"rime-frost-LICENSE.txt": next(e["sha256"] for e in config["sources"] if e["path"] == "LICENSE"),
               "rime-frost-NOTICE.md": sha256(ROOT / "docs/dictionary-frost-sources.md")}
    for name, digest in notices.items():
        if sha256(directory / name) != digest:
            raise ValueError(f"Missing/changed frost attribution: {name}")
    if manifest.get("pinyin_content_sha256") != config.get("expected_content_sha256"):
        raise ValueError("Manifest canonical identity differs from reviewed content")
    for name, digest in config["support"]["files"].items():
        # others.db 在 support 原件之上补了符号表（symbol_patch），改按补丁后的内容摘要核对。
        if name == "others.db":
            if symbol_patch.content_digest(directory / name) != symbol_patch.lock()["others_content_sha256"]:
                raise ValueError(f"Supporting dictionary changed: {name} "
                                     f"{symbol_patch.table_digests(directory / name)}")
            continue
        if sha256(directory / name) != digest:
            raise ValueError(f"Supporting dictionary changed: {name}")
    with closing(sqlite3.connect((directory / "msime.db").resolve().as_uri() + "?mode=ro", uri=True)) as connection:
        names = {row[0] for row in connection.execute("SELECT name FROM sqlite_master WHERE type='table'")}
        allowed = set(FORMAT.quanpin_tables()) | {"quick_parases", "latin_words", "sqlite_stat1", "sqlite_stat4"}
        if not set(FORMAT.quanpin_tables()) <= names or names - allowed:
            raise ValueError("Missing pinyin tables or retired/unknown tables present")
        if any(connection.execute(f'SELECT 1 FROM "{table}" LIMIT 1').fetchone() for table in ("quick_parases", "latin_words")):
            raise ValueError("Factory dictionary contains personal/mixed data")
        if connection.execute("PRAGMA integrity_check").fetchone() != ("ok",):
            raise ValueError("Invalid frost SQLite product")
        expected = config.get("expected_content_sha256")
        if not expected or canonical_digest(connection) != expected:
            raise ValueError("Pinyin records do not match reviewed canonical content")
    return manifest


def build(output: Path = OUT, cache: Path = CACHE, *, if_stale: bool = False) -> dict:
    output, cache = output.resolve(), cache.resolve()
    config = recipe()
    # Do not accidentally target a live profile, including via relative paths/junctions.
    import os
    live = Path(os.environ.get("LOCALAPPDATA", str(ROOT))) / "metasequoiaime"
    if output == live.resolve() or output.is_relative_to(live.resolve()):
        raise ValueError("Build output must not be an installed user data directory")
    if if_stale and (output / PRODUCT.MANIFEST_NAME).is_file():
        try:
            return verify(output, config)
        except (ValueError, OSError, sqlite3.Error):
            pass
    source = acquire(cache, config)
    overlay = load_overlay() if config.get("use_overlay") else None
    supplements_dir = cache / "supplements" if config.get("supplements") else None
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="incoming-frost-", dir=output.parent) as temporary:
        incoming = Path(temporary)
        result = build_pinyin(source, incoming / "msime.db", config,
                              supplements_dir=supplements_dir, overlay=overlay)
        expected = config.get("expected_content_sha256")
        if expected and result["canonical_sha256"] != expected:
            raise ValueError(f"Built pinyin content differs from reviewed lock: {result['canonical_sha256']}")
        write_json(incoming / "frost-build-report.json", result)
        for name in config["support"]["files"]:
            shutil.copyfile(cache / "support" / name, incoming / name)
        symbol_patch.patch(incoming / "others.db", cache)
        shutil.copyfile(source / "LICENSE", incoming / "rime-frost-LICENSE.txt")
        shutil.copyfile(ROOT / "docs/dictionary-frost-sources.md", incoming / "rime-frost-NOTICE.md")
        manifest = {"manifest_version": 1, "profile": "desktop", "format_version": 1,
                    "engine_compatibility": {"dictionary_format": 1}, "sqlite_journal_mode": "delete",
                    "source": {"repository": config["repository"], "commit": config["commit"], "dirty": False},
                    "recipe_sha256": sha256(LOCK), "builder_sha256": builder_digest(),
                    "pinyin_content_sha256": result["canonical_sha256"],
                    "features": ["pinyin", "quick_phrases", "english", "emoji", "kaomoji", "symbols"],
                    "source_packages": config["sources"],
                    "supplement_packages": config.get("supplements", []),
                    "supporting_dictionaries": config["support"],
                    "files": {name: {"sha256": sha256(incoming / name), "size": (incoming / name).stat().st_size}
                              for name in sorted(PRODUCT.DESKTOP_FILES)}}
        write_json(incoming / PRODUCT.MANIFEST_NAME, manifest)
        if expected:
            verify(incoming, config)
        shipping = sorted(path.name for path in incoming.iterdir())
        (incoming / "SHA256SUMS.txt").write_text("".join(f"{sha256(incoming / name)}  {name}\n" for name in shipping),
                                                  encoding="utf-8", newline="\n")
        # A whole-directory handoff with rollback, never update live database files one at a time.
        backup = output.with_name(output.name + ".previous")
        if backup.exists():
            raise ValueError(f"Previous generation already exists; review before rebuilding: {backup}")
        if output.exists():
            output.rename(backup)
        try:
            shutil.copytree(incoming, output)
        except BaseException:
            if output.exists():
                shutil.rmtree(output)
            if backup.exists():
                backup.rename(output)
            raise
        if backup.exists():
            shutil.rmtree(backup)
    return manifest
