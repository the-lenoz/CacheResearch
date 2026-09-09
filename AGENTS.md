# CacheResearch: руководство для агентов

## Назначение и текущее состояние

Это исследовательский проект для сравнения алгоритмов
вытеснения. Online-кэши шаблонизированы по ключу и значению и могут быть
собраны в эксклюзивную многоуровневую иерархию. CLI проигрывает traces с
`int`-ключами и пустым payload (`std::monostate`). Belady реализован отдельно
как offline-эталон.

Уже реализованы LRU, LFU, 2Q, ARC, LIRS, Belady, фабрика, иерархия, парсинг
конфигов, генераторы конфигов/workloads и параллельный benchmark. Ёмкость везде
измеряется количеством элементов, не байтами. Потокобезопасности нет.

## Структура и сборка

```text
src/cache.cppm          общий Cache<Key, Value> и CacheEntry
src/{lru,lfu}.cppm      простые online-политики
src/{two_q,arc,lirs}.cppm  политики с shadow/ghost history
src/belady.cppm         offline optimum, не наследует Cache
src/hierarchy.cppm      эксклюзивная L1 -> L2 -> ... иерархия
src/factory.cppm        создание online-политик по имени
src/config/             единственное разделение .cppm + .cpp
src/main.cpp            CLI cache_sim
cache_benchmarks.ipynb  запуск benchmark и графики hit-rate/Belady/Pareto
tests/                  GoogleTest, один файл на компонент
scripts/                генераторы и benchmark
configs/                ручные конфиги; generated/ создаётся скриптом
workloads/, results/    генерируемые, игнорируются Git
```

Требуются CMake >= 3.28, Ninja и компилятор с поддержкой C++20 modules.

```bash
cmake --preset debug-tests
cmake --build --preset debug-tests --parallel
ctest --preset debug-tests

cmake --preset release
cmake --build --preset release --parallel
```

`cache_lib` содержит module interfaces и `config.cpp`; `cache_sim` линкуется с
ним. Тесты включаются только через `CACHE_RESEARCH_BUILD_TESTS`/preset
`debug-tests`. GoogleTest сначала ищется в системе, иначе получается через
FetchContent.

### Правила C++ modules

- Шаблонный модуль — один файл `src/<name>.cppm`; вся реализация шаблона должна
  быть видима в interface unit. Не создавать для него папку и пустой `.cpp`. При этом если требуется 
большая, не шаблонизированая логика, которую можно вынести в `.cpp` - вынеси и сложи в подпапку src. 
Это принцип чистоты архитектуры 
- `config` оставлять в `src/config/`: публичные объявления находятся в
  `config.cppm`, нетемплейтные определения — в `config.cpp`. Это и есть пример чисто вынесенной логики.
- Стандартные `#include` размещать в global module fragment после `module;`,
  затем писать `export module cache.<name>;`, после него — `import cache;` и
  другие module imports.
- Новый `.cppm` обязательно добавить в `FILE_SET CXX_MODULES` корневого CMake.
  Новый test-файл добавить в `tests/CMakeLists.txt`.

## Общая модель и поток выполнения

Конфиг содержит число уровней и по одному имени политики на уровень:

```text
3
LRU
ARC
LFU
```

Trace приходит через stdin:

```text
<capacity_per_level> <request_count>
<key_1> ... <key_n>
```

Для online-конфига CLI создаёт каждый уровень с одной и той же ёмкостью,
вызывает `hierarchy.access(key)` и на miss делает `hierarchy.insert({key, {}})`.
Печатается ровно одно целое число — количество hits. Единственный `BELADY`
переключает CLI в offline-flow. Ошибки печатаются в stderr с префиксом `error:`
и дают код возврата 1.

## Контракт `Cache<KeyType, ValueType>`

`CacheEntry` владеет `key` и `value`. Реализации принимают также шаблонные
`Hash` и `KeyEqual`; текущие структуры требуют хешируемый, сравнимый и
копируемый ключ, а также move-constructible и move-assignable значение.

- `find(key)`/`const find(key)` — только поиск resident value. Возвращает
  указатель или `nullptr`; не меняет policy state и не считает hit.
- `touch(key)` — регистрирует hit существующего resident key; отсутствующий
  ключ должен быть no-op.
- `insert(entry)` — вставляет новый resident item. При полном кэше возвращает
  вытесненный `CacheEntry`, иначе `nullopt`. При `capacity == 0` немедленно
  возвращает входной entry и ничего не хранит. Повторный key обновляет value,
  учитывается политикой как обращение и не вытесняет другой item.
- `extract(key)` — удаляет и возвращает resident entry или `nullopt`. Это
  техническое перемещение между уровнями: оно не должно создавать ghost/shadow
  history, как обычное вытеснение.
- `erase(key)` — невозвращающий wrapper над `extract`.
- `clear()` очищает resident state, history и адаптивные счётчики.
- `size()` считает только resident items и всегда `<= capacity()`.
- `shadow_size()`/`shadow_capacity()` считают только non-resident keys. Базовая
  реализация возвращает нули; политики с history переопределяют оба метода и
  держат `shadow_size() <= shadow_capacity()`.
- Указатели из `find()` действуют только до следующей мутирующей операции.

Индекс и списки одной политики должны всегда изменяться согласованно. Значение
хранится ровно в одном resident-узле; ghost-структуры хранят только ключи.

## `CacheHierarchy`

Конструктор принимает непустой `vector<unique_ptr<Cache<...>>>`; порядок — от
L1 к последнему уровню. Уровни могут иметь разные policy и capacity, хотя CLI
сейчас даёт им одинаковую capacity. Иерархия эксклюзивная: resident key должен
находиться только на одном уровне.

- `access(key) -> ValueType*`: ищет сверху вниз. Miss возвращает `nullptr`, не
  вставляет значение и не увеличивает `hits()`. Hit в L1 вызывает `touch`.
  Hit ниже делает `extract`, вставляет сохранённый entry в L1 и каскадно
  проталкивает вытеснения вниз. Возвращается сохранённое значение, а hits
  увеличивается один раз.
- `insert(entry)`: явно заполняет L1. Перед этим удаляет тот же resident key из
  нижних уровней, чтобы сохранить exclusivity. Каждый возвращённый victim
  вставляется на следующий уровень; victim последнего уровня теряется.
- `find(key)` (включая const overload) только ищет и не меняет порядок/частоты.
- `hits()` — накопительный счётчик успешных `access`; reset/clear пока нет.

Обычный real-data flow:

```cpp
if (auto* value = hierarchy.access(key)) {
    return *value;
}
auto value = load_from_storage(key);
hierarchy.insert({key, value});
return value;
```

## Реализованные политики

### LRU (`cache.lru`)

`list<CacheEntry>` хранит MRU спереди и LRU сзади; hash index указывает на
итераторы списка. `touch`/повторный insert делают `splice` в начало. При
переполнении вытесняется хвост. Основные операции O(1) в среднем.

### LFU (`cache.lfu`)

Упорядоченный список frequency buckets идёт по возрастанию частоты; внутри
bucket ключи расположены от MRU к LRU. Новый key получает frequency 1. `touch`
переносит его в соседний bucket `frequency + 1`. Victim — LRU-ключ из первого
(минимального) bucket, то есть ties разрешаются по LRU.

### 2Q (`cache.two_q`)

Resident queues: `A1in` для новых и `Am` для повторно использованных; `A1out`
— ограниченная ghost FIFO/LRU-история ключей, вытесненных из `A1in`. Новый key
идёт в `A1in`, hit в `A1in` переводит его в `Am`, hit в `Am` делает его MRU.
Повторная вставка key из `A1out` сразу идёт в `Am`. Вытеснения из `Am` в ghost
не записываются.

Текущие эвристики помечены `TODO(tuning)`: `A1in = max(1, capacity/4)`, default
`A1out = max(1, capacity/2)` (для zero capacity оба ноль). Второй аргумент
конструктора задаёт точный `max_shadow_items`.

### ARC (`cache.arc`)

Residents: `T1` (recent) и `T2` (frequent); ghosts: `B1` для T1 и `B2` для T2.
Hit переводит T1 -> T2 или обновляет MRU в T2. Новый key идёт в T1. Ghost hit
идёт в T2 и адаптирует `target_t1_size_`: B1 увеличивает цель на
`max(1, |B2|/|B1|)`, B2 уменьшает на `max(1, |B1|/|B2|)`. Replacement выбирает
между LRU T1 и LRU T2 относительно этой цели.

Начальная цель T1 равна 0, default общий лимит `B1+B2` равен resident capacity;
оба решения и предпочтение при trimming помечены `TODO(tuning)`. Второй
аргумент конструктора задаёт общий shadow limit. `extract` удаляет resident без
добавления в B1/B2.

### LIRS (`cache.lirs`)

Stack S хранит recency LIR и историю HIR; queue Q хранит только resident HIR.
`entries_` содержит resident nodes с `optional<ValueType>` и non-resident HIR
nodes без value. LIR hit переносится наверх S. HIR, повторно найденный в S,
становится LIR, а нижний LIR демотируется в HIR/Q; прочий HIR остаётся HIR.
Victim обычно берётся с конца Q и остаётся shadow, только если ещё присутствует
в S. Stack pruning заканчивает S на LIR и удаляет ненужную историю.

Default LIR quota: `capacity - max(1, capacity/100)`, то есть остаётся минимум
один resident HIR slot. Default shadow limit равен capacity; второй аргумент
задаёт его явно. Доля HIR и выбор удаляемой истории помечены `TODO(tuning)`.

### Belady (`cache.belady`)

`BeladyCache<Key>` принимает capacity и весь trace, payload не хранит и
`Cache` не реализует. `run()` сначала обратным проходом вычисляет следующее
использование каждого запроса, затем держит residents в hash map и ordered set.
При miss вытесняется key с самым далёким следующим использованием (`never` —
самый выгодный victim). Результат — только hit count; сложность O(n log C).

## Factory и config

`make_cache<Key, Value, Hash, KeyEqual>(policy, capacity, shadow_capacity)`
поддерживает точные uppercase-имена `LRU`, `LFU`, `2Q`, `ARC`, `LIRS`.
Неизвестное имя вызывает `invalid_argument`. Optional shadow capacity передаётся
только 2Q/ARC/LIRS; для LRU/LFU она игнорируется. Belady намеренно не входит в
factory.

`parse_config` проверяет положительное число уровней и наличие заявленного
количества имён, но не валидирует имена и не запрещает лишние токены — policy
валидирует factory. `read_input` читает capacity, request count и ровно столько
`DefaultKey`; неполный ввод вызывает `runtime_error`.

## Экспериментальный pipeline

- `generate_configs.py`: по умолчанию генерирует декартово произведение пяти
  online-политик для трёх уровней (`5^3 = 125`), включая повторы. Есть выбор
  levels/policies, режим без повторов и `--clean`.
- `generate_workloads.py`: детерминированно по derived seed создаёт `loop`,
  `scan`, `uniform`, clipped normal, 80/20 `hotset`, чередующий hot/scan,
  четырёхфазный `phase_change` и Zipf (exponent 1.1). `capacity` в trace — на
  один уровень; total capacity для паттерна равна `capacity * levels`. Working
  set у `loop` и каждой phase равен `total_capacity + 1`, чтобы вся иерархия не
  могла его вместить. Перед добавлением паттерна обновить `PATTERNS`, CLI choices
  и тестировать граничные размеры.
- `benchmark.py`: читает все `.conf`/`.trace`, запускает пары параллельно с
  timeout, ожидает одно целое число stdout. Для каждого количества уровней
  запускает Belady с capacity `capacity_per_level * levels`. Пишет полный CSV,
  winner на workload и агрегированный winner на pattern; равенства разрешаются
  лексикографически по имени конфига. Любой failed/ideal_failed run даёт exit 1.
- `cache_benchmarks.ipynb`: запускает штатный benchmark из корня проекта,
  читает полный CSV и визуализирует top-5 по проценту от Belady, специализацию
  по pattern, устойчивость, зависимость от capacity и Pareto hit-rate/runtime.
- CMake targets `generate_configs`, `generate_workloads`, `benchmark` доступны,
  если найден Python >= 3.10; `benchmark` зависит от `cache_sim` текущего build
  tree и обоих генераторов. Штатный полный запуск делается из Release preset.

## Как добавить или переписать online-политику

1. Создать `src/<name>.cppm`, импортировать `cache`, унаследоваться от
   `Cache<Key, Value>` и реализовать весь контракт, включая const `find`, zero
   capacity, обновление существующего key и безопасный `extract`.
2. Для списков плюс hash index явно поддерживать валидность итераторов после
   каждого insert/touch/extract/clear. Если есть ghosts, хранить там только keys
   и соблюдать shadow limit, включая limit 0.
3. Добавить import/ветку в `factory.cppm`, имя в `SUPPORTED_POLICIES` генератора
   конфигов, module interface в CMake и `<name>_test.cpp` в tests/CMakeLists.
4. Минимальные тесты: victim/order, повторный hit, payload update, extract,
   capacity 0, custom Hash/Key при необходимости; для history-policy — ghost
   hit и shadow limits 0/ненулевой. Проверить работу внутри hierarchy.
5. Выполнить Debug+CTest и Release build. Изменения CLI/pipeline дополнительно
   проверить коротким trace; stdout должен оставаться машинно-читаемым.

## Следующее ожидаемое развитие и известные ограничения

- Вынести помеченные `TODO(tuning)` параметры 2Q/ARC/LIRS в policy config,
  сохранив нынешние значения defaults и обратную совместимость factory.
- Добавить режим общего бюджета `resident items + shadow items`; сейчас resident
  capacity и `max_shadow_items` независимы, API лишь позволяет наблюдать оба.
- Расширить формат config до параметров и, вероятно, отдельных capacities на
  уровень. Сейчас он задаёт только список policies, а CLI одинаково делит
  capacity по уровням.
- Для прикладного key-value использования поверх уже разделённых
  `access(key)`/`insert(entry)` можно добавить `get_or_load`, но loader обязан
  вызываться только на miss. Не возвращать старый объединённый `access(entry)`.
- Иерархия пока отбрасывает victim последнего уровня, не возвращает per-level
  metrics и не имеет reset/clear. Менять это нужно отдельным явным контрактом,
  не скрытой побочной семантикой существующих методов.

При изменении алгоритмов считать тесты описанием наблюдаемого поведения, но
сохранять перечисленные выше публичные контракты и инварианты независимо от
внутренних структур данных.

Если ты меняешь что-то, описанное в этом файле - необходимо актуализировать его.
