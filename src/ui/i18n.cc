#include "src/ui/i18n.h"

#include <array>
#include <cctype>
#include <unordered_map>

namespace usa::ui {
namespace {

using Entry = std::array<const char*, 4>;  // en, es, zh-TW, ja

const std::unordered_map<std::string_view, Entry>& Table() {
  static const auto* table = new std::unordered_map<std::string_view, Entry>{
      {"breaking", {"BREAKING", "ÚLTIMA HORA", "快訊", "速報"}},
      {"feed.title", {"Election night feed", "En directo", "開票快訊", "開票速報"}},
      {"callout.counted", {"{0} in", "{0} escrutado", "開票 {0}", "開票率 {0}"}},
      {"lead.text", {"{0} leads {1} by {2}", "{0} aventaja a {1} por {2}", "{0} 領先 {1} {2} 票",
                     "{0}が{1}を{2}票リード"}},
      {"flip.text", {"{0} overtakes {1}", "{0} supera a {1}", "{0} 反超 {1}", "{0}が{1}を逆転"}},
      {"stamp.elected", {"WIN", "GANA", "當選", "当選"}},
      {"chip.declared", {"Declared", "Proclamó", "宣布勝選", "勝利宣言"}},
      {"chip.conceded", {"Conceded", "Reconoció", "承認敗選", "敗北宣言"}},
      {"timeline.paused", {"Paused", "Pausa", "暫停", "一時停止"}},
      {"timeline.et", {"ET", "ET", "美東", "東部"}},
      {"ev.after_declaration",
       {" (after declaring victory)", " (tras proclamarse ganador)", "（曾宣布勝選）", "（勝利宣言後）"}},
      {"ev.first.title", {"First returns", "Primeros resultados", "開出首票", "開票開始"}},
      {"ev.first.text",
       {"{0}: first returns, {1} ahead", "{0}: primeros votos, {1} adelante",
        "{0}：開出首批選票，{1} 暫時領先", "{0}：開票開始、{1}が先行"}},
      {"ev.lead_change.title", {"Lead change", "Cambio de líder", "逆轉", "逆転"}},
      {"ev.lead_change.text",
       {"{0}: {1} overtakes {2}", "{0}: {1} supera a {2}", "{0}：{1} 反超 {2}", "{0}：{1}が{2}を逆転"}},
      {"ev.victory.title", {"Victory declared", "Se proclama ganador", "宣布勝選", "勝利宣言"}},
      {"ev.victory.text",
       {"{0}: {1} declares victory", "{0}: {1} se proclama ganador", "{0}：{1} 宣布勝選",
        "{0}：{1}が勝利宣言"}},
      {"ev.concede.title", {"Concession", "Reconoce la derrota", "承認敗選", "敗北宣言"}},
      {"ev.concede.text",
       {"{0}: {1} concedes", "{0}: {1} reconoce la derrota", "{0}：{1} 承認敗選",
        "{0}：{1}が敗北を認める"}},
      {"ev.called.title", {"Race call", "Proyección", "勝選預測", "当選確実"}},
      {"ev.called.text",
       {"{0}: {1} projected to win", "{0}: se proyecta la victoria de {1}", "{0}：預測 {1} 勝選",
        "{0}：{1}が当選確実"}},
      {"ev.incumbent_trailing.title",
       {"Incumbent trailing", "Titular por detrás", "現任落後", "現職劣勢"}},
      {"ev.incumbent_trailing.text",
       {"{0}: incumbent {2} trails {1}", "{0}: el titular {2} va detrás de {1}",
        "{0}：現任 {2} 落後 {1}", "{0}：現職の{2}が{1}を追う展開"}},
      {"ev.close.title", {"Too close to call", "Muy reñido", "差距膠著", "大接戦"}},
      {"ev.close.text",
       {"{0}: {1} and {2} just {3} votes apart", "{0}: {1} y {2}, a solo {3} votos",
        "{0}：{1} 與 {2} 僅差 {3} 票", "{0}：{1}と{2}の差わずか{3}票"}},
      {"ev.final.title", {"Count complete", "Escrutinio completo", "計票完畢", "開票終了"}},
      {"ev.final.text",
       {"{0}: count complete, {1} wins by {3}", "{0}: escrutinio completo, {1} gana por {3}",
        "{0}：計票完畢，{1} 勝出 {3} 票", "{0}：開票終了、{1}が{3}票差で勝利"}},
      {"ev.flip.title", {"Seat flip", "Cambio de partido", "政黨輪替", "議席奪取"}},
      {"ev.flip.text",
       {"{0}: {4} flip — {1} takes the seat from {5}", "{0}: el {4} arrebata el escaño al {5} ({1})",
        "{0}：{4} 從 {5} 手中翻盤（{1}）", "{0}：{4}が{5}から奪取（{1}）"}},
      {"ev.polls_closed.title", {"Polls close", "Cierre de urnas", "投票結束", "投票締め切り"}},
      {"ev.polls_closed.text",
       {"Polls closed in {0}", "Cierran las urnas en {0}", "{0} 投票結束", "{0}で投票締め切り"}},
      {"ev.control.title", {"Majority clinched", "Mayoría asegurada", "取得多數", "過半数確保"}},
      {"ev.control.text",
       {"{1} wins control of the {0} ({2} seats)", "{1} controlará el {0} ({2} escaños)",
        "{1}取得{0}多數（{2} 席）", "{1}が{0}の過半数を確保（{2}議席）"}},
      {"pin.pinned", {"Pinned as home", "Fijado como inicio", "已釘選為首頁", "ホームに固定"}},
      {"pin.unpinned", {"Home unpinned", "Inicio desfijado", "已取消釘選", "固定を解除"}},
      {"news.title", {"News sentiment", "Tono de las noticias", "新聞輿情", "ニュース評判"}},
      {"news.totals",
       {"Good {0} · bad {1} · neutral {2}", "Buenas {0} · malas {1} · neutras {2}",
        "利多 {0} · 利空 {1} · 中性 {2}", "好材料 {0} · 悪材料 {1} · 中立 {2}"}},
      {"news.disabled",
       {"News off: start with --news (OpenAI-compatible API)", "Noticias desactivadas: use --news",
        "未啟用新聞：以 --news 啟動", "ニュース無効：--news で起動"}},
      {"news.good", {"News · good", "Noticia · buena", "新聞 · 利多", "ニュース · 好材料"}},
      {"news.bad", {"News · bad", "Noticia · mala", "新聞 · 利空", "ニュース · 悪材料"}},
      {"news.neutral", {"News · neutral", "Noticia · neutra", "新聞 · 中性", "ニュース · 中立"}},
      {"news.simulated", {"[SIMULATED] ", "[SIMULADO] ", "【模擬】", "【模擬】"}},
      {"chart.map", {"Map", "Mapa", "地圖", "地図"}},
      {"chart.trend", {"Trend", "Tendencia", "得票走勢", "得票推移"}},
      {"chart.seats", {"Seats", "Escaños", "席次", "議席"}},
      {"chart.margins", {"Margins", "Márgenes", "差距排行", "接戦順"}},
      {"chart.parties", {"Parties", "Partidos", "政黨得票", "政党別"}},
      {"chart.grid", {"Grid", "Cuadrícula", "總覽", "一覧"}},
      {"chart.back", {"M / Esc back to map", "M / Esc volver al mapa", "M / Esc 回到地圖",
                      "M / Esc 地図に戻る"}},
      {"chart.nodata", {"No votes counted yet", "Aún no hay votos", "尚無開票資料", "開票データなし"}},
      {"trend.title",
       {"{0} · vote share over the night", "{0} · voto a lo largo de la noche", "{0} · 得票率走勢",
        "{0} · 得票率の推移"}},
      {"trend.hint", {"← → switch race · times ET", "← → cambiar contienda · hora ET",
                      "← → 切換選區 · 美東時間", "← → 選挙区切替 · 米東部時間"}},
      {"seats.title.senate",
       {"Senate · 100 seats · control: 51, or 50 + VP ({0})",
        "Senado · 100 escaños · control: 51, o 50 + VP ({0})",
        "參議院 · 100 席 · 過半：51 席，或 50 席＋副總統（{0}）",
        "上院 · 100 議席 · 過半数：51、または 50＋副大統領（{0}）"}},
      {"seats.title.house",
       {"House · 435 seats · majority 218", "Cámara · 435 escaños · mayoría 218",
        "眾議院 · 435 席 · 過半 218", "下院 · 435 議席 · 過半数 218"}},
      {"seats.title.governor",
       {"Governors · 50 states · {0} races tonight", "Gobernadores · 50 estados · {0} en juego",
        "州長 · 50 州 · 今晚改選 {0} 席", "州知事 · 50 州 · 今回 {0} 州で選挙"}},
      {"seats.majority", {"Majority {0}", "Mayoría {0}", "過半 {0}", "過半数 {0}"}},
      {"seats.projected", {"Called", "Proyectado", "勝選預測", "当選確実"}},
      {"seats.leading", {"Leading", "Adelante", "領先中", "リード中"}},
      {"seats.holdover", {"Not up", "No en juego", "非改選", "非改選"}},
      {"seats.split", {"{0} called · {1} ahead", "{0} proyect. · {1} adelante", "確定 {0} · 領先 {1}",
                       "確実 {0} · リード {1}"}},
      {"seats.total", {"{0} seats", "{0} escaños", "{0} 席", "{0} 議席"}},
      {"margins.title",
       {"{0} · closest races first", "{0} · las más reñidas primero", "{0} · 最接近的選戰在最上方",
        "{0} · 接戦順"}},
      {"margins.hint",
       {"Leader minus runner-up, in points · click to open on the map",
        "Ventaja del líder en puntos · clic para ver en el mapa",
        "領先者與第二名的得票率差距 · 點選查看地圖", "1位と2位の得票率差 · クリックで地図へ"}},
      {"parties.title",
       {"Votes by party · {0} races", "Votos por partido · {0}", "各政黨總得票 · {0}選舉",
        "政党別総得票 · {0}選挙"}},
      {"parties.total", {"Total votes {0}", "Votos totales {0}", "總票數 {0}", "総得票 {0}"}},
      {"parties.seats", {"Won {0} · ahead {1}", "Gana {0} · adelante {1}", "勝選 {0} · 領先 {1}",
                         "当選 {0} · リード {1}"}},
      {"parties.house",
       {"House seats called {0} of 435", "Escaños de la Cámara proyectados: {0} de 435",
        "眾議院已確定 {0} / 435 席", "下院 当選確実 {0} / 435"}},
      {"grid.title", {"All {0} {1} races", "Las {0} contiendas · {1}", "{1} · 全部 {0} 場",
                      "{1} · 全 {0} 選挙"}},
      {"grid.house_title",
       {"House · seats called by state", "Cámara · escaños proyectados por estado",
        "眾議院 · 各州已確定席次", "下院 · 州別の当選確実"}},
      {"grid.margin", {"Margin {0}", "Ventaja {0}", "差距 {0} 票", "差 {0} 票"}},
      {"pip.title", {"Latest", "Lo último", "最新快訊", "最新ニュース"}},
      {"app.title", {"2026 U.S. Midterms", "Elecciones EE. UU. 2026", "2026 美國期中選舉",
                     "2026 米国中間選挙"}},
      {"window.title",
       {"2026 United States Elections", "Elecciones de Estados Unidos 2026", "2026 美國期中選舉",
        "2026 米国中間選挙"}},
      {"status.simulation",
       {"SIMULATION", "SIMULACIÓN", "模擬", "模擬"}},
      {"status.voting", {"Polls open", "Urnas abiertas", "投票進行中", "投票中"}},
      {"status.awaiting", {"Awaiting results", "Esperando resultados", "等待開票資料", "開票データ待ち"}},
      {"status.countdown", {"{0} days to Election Day", "Faltan {0} días", "距投票日 {0} 天",
                            "投票日まで {0} 日"}},
      {"status.counting", {"Counting", "Escrutinio", "計票中", "開票中"}},
      {"status.final", {"Final", "Final", "計票完成", "開票終了"}},
      {"header.subtitle",
       {"Tue {0} · Senate {1} · Governors {2} · House 435",
        "Mar {0} · Senado {1} · Gobernadores {2} · Cámara 435",
        "{0}（週二）· 參議員 {1} · 州長 {2} · 眾議員 435",
        "{0}（火）· 上院 {1} · 州知事 {2} · 下院 435"}},
      {"office.senate", {"Senate", "Senado", "參議院", "上院"}},
      {"office.governor", {"Governors", "Gobernadores", "州長", "州知事"}},
      {"office.house", {"House", "Cámara", "眾議院", "下院"}},
      {"office.hint", {"O switch", "O cambiar", "O 切換", "O 切替"}},
      {"race.senate", {"{0} Senate", "Senado · {0}", "{0} 參議員", "{0} 上院"}},
      {"race.senate_special",
       {"{0} Senate (special)", "Senado · {0} (especial)", "{0} 參議員（補選）", "{0} 上院（補欠）"}},
      {"race.governor", {"{0} Governor", "Gobernador · {0}", "{0} 州長", "{0} 知事"}},
      {"bop.title", {"Balance of power", "Equilibrio de poder", "國會與州長版圖", "勢力図"}},
      {"bop.senate", {"Senate · 51 to control", "Senado · 51 para el control", "參議院 · 51 席過半",
                      "上院 · 過半数 51"}},
      {"bop.house", {"House · 218 to control", "Cámara · 218 para el control", "眾議院 · 218 席過半",
                     "下院 · 過半数 218"}},
      {"bop.governors", {"Governors", "Gobernadores", "州長", "州知事"}},
      {"bop.called", {"{0} of {1} races called", "{0} de {1} proyectadas", "已預測 {0} / {1} 場",
                      "{0} / {1} 当選確実"}},
      {"bop.house_called", {"{0} of 435 seats called", "{0} de 435 proyectados",
                            "已確定 {0} / 435 席", "{0} / 435 当選確実"}},
      {"bop.vp", {"VP {0} breaks ties", "El VP {0} desempata", "副總統 {0} 可投決勝票",
                  "副大統領 {0} が決定票"}},
      {"state.title", {"State overview", "Resumen del estado", "本州概況", "州の概要"}},
      {"state.counties", {"{0} counties", "{0} condados", "{0} 個郡", "{0} 郡"}},
      {"state.polls", {"Last polls close {0} ET", "Cierre de urnas {0} ET", "投票截止 美東 {0}",
                       "投票締め切り 米東部 {0}"}},
      {"state.house", {"House delegation", "Delegación en la Cámara", "眾議院席次", "下院議席"}},
      {"state.governor", {"Governor", "Gobernador", "州長", "知事"}},
      {"state.no_race", {"No race this year", "Sin elección este año", "今年未改選", "今回は選挙なし"}},
      {"area.incumbent", {"Incumbent", "Titular", "現任", "現職"}},
      {"area.retiring", {"Retiring", "Se retira", "不尋求連任", "引退"}},
      {"area.term_limited", {"Term-limited", "Sin reelección", "任期屆滿", "任期制限"}},
      {"area.running", {"Running", "Se presenta", "尋求連任", "再選目指す"}},
      {"nation.title", {"{0} · {1} races", "{0} · {1} contiendas", "{0} · {1} 場", "{0} · {1} 選挙"}},
      {"nation.candidates", {"{0} candidates", "{0} candidatos", "{0} 位候選人", "候補者 {0} 人"}},
      {"nation.seats_final", {"Won, by party", "Ganados, por partido", "勝選（依政黨）",
                              "当選（政党別）"}},
      {"nation.seats_leading", {"Leading, by party", "Adelante, por partido", "目前領先（依政黨）",
                                "リード中（政党別）"}},
      {"nation.seats_incumbent", {"Held now, by party", "En poder de, por partido", "現任（依政黨）",
                                  "現職（政党別）"}},
      {"nation.count", {"{0}", "{0}", "{0} 人", "{0} 人"}},
      {"house.title", {"U.S. House · 435 seats", "Cámara de Representantes · 435",
                       "聯邦眾議院 · 435 席", "連邦下院 · 435 議席"}},
      {"house.before", {"Before: {0}", "Antes: {0}", "改選前：{0}", "改選前：{0}"}},
      {"house.state_title", {"{0} · House delegation", "{0} · delegación en la Cámara",
                             "{0} · 眾議院席次", "{0} · 下院議席"}},
      {"house.seats", {"{0} seats", "{0} escaños", "{0} 席", "{0} 議席"}},
      {"house.called", {"{0} of {1} called", "{0} de {1} proyectados", "已確定 {0} / {1}",
                        "{0} / {1} 確実"}},
      {"house.note",
       {"Seat calls are simulated from the sitting delegations (2024 lines).",
        "Proyecciones simuladas a partir de las delegaciones actuales.",
        "席次為依現任席次模擬（2024 年選區）。", "議席は現職の構成から模擬（2024年区割り）。"}},
      {"race.none", {"No {0} race here this year", "No hay elección de {0} este año",
                     "今年此處無{0}選舉", "今回ここでは{0}選挙なし"}},
      {"race.scope_state", {"Statewide results", "Resultados del estado", "全州計票結果",
                            "州全体の開票結果"}},
      {"race.scope_region", {"Results in {0}", "Resultados en {0}", "{0} 計票結果", "{0} の開票結果"}},
      {"race.progress",
       {"{0} / {1} county batches in ({2})", "{0} / {1} lotes de condados ({2})",
        "已回報 {0} / {1} 批（{2}）", "報告 {0} / {1} 件（{2}）"}},
      {"race.not_started", {"No votes reported yet", "Aún sin votos", "尚未開票", "未開票"}},
      {"race.turnout", {"Turnout {0}", "Participación {0}", "投票率 {0}", "投票率 {0}"}},
      {"race.incumbent", {"Incumbent", "Titular", "現任", "現職"}},
      {"race.elected", {"Winner", "Ganador", "當選", "当選"}},
      {"race.leading", {"Leading", "Adelante", "領先", "リード"}},
      {"race.backed", {"backed by {0}", "apoyado por {0}", "{0} 支持", "{0} 支援"}},
      {"race.totals",
       {"Total votes {0} · registered {1}", "Votos {0} · registrados {1}", "總票數 {0} · 登記選民 {1}",
        "得票計 {0} · 登録有権者 {1}"}},
      {"race.order_note",
       {"General-election nominees as of Sep 29, 2026", "Candidatos a 29 sep 2026",
        "普選候選人（截至 2026/9/29）", "本選挙の候補者（2026/9/29 時点）"}},
      {"level.nation", {"Nation", "País", "全國", "全国"}},
      {"level.state", {"State", "Estado", "州", "州"}},
      {"level.county", {"County", "Condado", "郡", "郡"}},
      {"level.precinct", {"Precinct", "Distrito", "選區", "投票区"}},
      {"tip.no_votes_enter", {"No votes yet · click to open", "Sin votos · clic para abrir",
                              "尚無開票數據 · 點擊進入", "開票データなし · クリックで詳細"}},
      {"tip.no_votes", {"No votes yet", "Sin votos", "尚無開票數據", "開票データなし"}},
      {"tip.enter", {"Click to open", "Clic para abrir", "點擊進入", "クリックで詳細"}},
      {"tip.no_race", {"No race for this office", "Sin elección para este cargo", "此職位未改選",
                       "この職は選挙なし"}},
      {"inset", {"Not to scale", "Sin escala", "非實際比例", "縮尺は模式的"}},
      {"footer.controls",
       {"Left-drag pan · right-drag orbit/tilt · wheel zoom · click drill down · Esc up · O office: {1} · "
        "1–3 colour: {0} · F2–F6/G charts · I PiP · N news · P pin · L language · H help",
        "Arrastrar mover · clic derecho girar · rueda zoom · clic entrar · Esc subir · O cargo: {1} · "
        "1–3 color: {0} · F2–F6/G gráficos · I PiP · N noticias · P fijar · L idioma · H ayuda",
        "左鍵拖曳 平移 · 右鍵 旋轉 · 滾輪 縮放 · 點擊 進入 · Esc 返回 · O 職位：{1} · 1–3 著色：{0} · "
        "F2–F6/G 圖表 · I 子母畫面 · N 新聞 · P 釘選 · L 語言 · H 說明",
        "左ドラッグ 移動 · 右ドラッグ 回転 · ホイール ズーム · クリック 詳細 · Esc 戻る · O 職：{1} · "
        "1–3 配色：{0} · F2–F6/G グラフ · I PiP · N ニュース · P 固定 · L 言語 · H ヘルプ"}},
      {"mode.leader", {"Leading party", "Partido en cabeza", "領先政黨", "リード政党"}},
      {"mode.progress", {"Votes reported", "Votos escrutados", "開票進度", "開票率"}},
      {"mode.turnout", {"Turnout", "Participación", "投票率", "投票率"}},
      {"help.title", {"Controls", "Controles", "操作說明", "操作方法"}},
      {"help.1", {"Left-drag — pan the map", "Arrastrar — mover el mapa", "左鍵拖曳 — 平移地圖",
                  "左ドラッグ — 地図を移動"}},
      {"help.2", {"Right/middle-drag — orbit & tilt", "Botón derecho/central — girar e inclinar",
                  "右鍵 / 中鍵拖曳 — 旋轉與傾斜", "右 / 中ドラッグ — 回転・傾き"}},
      {"help.3", {"Wheel, + / - — zoom to cursor", "Rueda, + / - — zoom", "滾輪、+ / - — 縮放",
                  "ホイール、+ / - — ズーム"}},
      {"help.4", {"Click — drill into state → county", "Clic — entrar en estado → condado",
                  "左鍵點擊 — 進入州 → 郡", "クリック — 州 → 郡へ"}},
      {"help.5",
       {"Esc / Backspace / right-click — go up a level", "Esc / Retroceso / clic derecho — subir",
        "Esc / Backspace / 右鍵點擊 — 返回上一層", "Esc / Backspace / 右クリック — 一つ上へ"}},
      {"help.6",
       {"O — Senate / Governors / House · 1 leader · 2 reported · 3 turnout",
        "O — Senado / Gobernadores / Cámara · 1 líder · 2 escrutado · 3 participación",
        "O — 參議院 / 州長 / 眾議院 · 1 領先 · 2 進度 · 3 投票率",
        "O — 上院 / 知事 / 下院 · 1 リード · 2 開票率 · 3 投票率"}},
      {"help.7",
       {"Arrows / WASD — pan · Q / E — rotate · R / F — tilt",
        "Flechas / WASD — mover · Q / E — girar · R / F — inclinar",
        "方向鍵 / WASD — 平移 · Q / E — 旋轉 · R / F — 傾斜",
        "矢印 / WASD — 移動 · Q / E — 回転 · R / F — 傾き"}},
      {"help.8",
       {"Space pause · , / . speed ×½ / ×2 · [ / ] ∓30 min · P pin as home",
        "Espacio pausa · , / . velocidad ×½ / ×2 · [ / ] ∓30 min · P fijar inicio",
        "Space 暫停 · , / . 速度 ×½ / ×2 · [ / ] 倒轉/快轉 30 分 · P 釘選首頁",
        "Space 一時停止 · , / . 速度 ×½ / ×2 · [ / ] 30分戻す/進める · P ホーム固定"}},
      {"help.9",
       {"L — language (EN / ES / 繁中 / 日本語) · Home — home region · N — news · H — close",
        "L — idioma (EN / ES / 繁中 / 日本語) · Inicio — región · N — noticias · H — cerrar",
        "L — 切換語言 · Home — 回首頁區域 · N — 新聞 · H — 說明",
        "L — 言語切替 · Home — ホームへ · N — ニュース · H — 閉じる"}},
  };
  return *table;
}

}  // namespace

bool ParseLang(std::string_view s, Lang* out) {
  std::string l;
  for (char c : s) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (l == "en" || l == "en-us" || l == "en_us" || l == "en-gb") {
    *out = Lang::kEn;
  } else if (l == "es" || l == "es-us" || l == "es_us" || l == "es-mx" || l == "es-es") {
    *out = Lang::kEs;
  } else if (l == "zh" || l == "zh-tw" || l == "zh_tw" || l == "tw" || l == "zh-hant") {
    *out = Lang::kZhTW;
  } else if (l == "ja" || l == "jp" || l == "ja-jp" || l == "ja_jp") {
    *out = Lang::kJa;
  } else {
    return false;
  }
  return true;
}

const char* LangCode(Lang l) {
  switch (l) {
    case Lang::kEs: return "es";
    case Lang::kZhTW: return "zh-TW";
    case Lang::kJa: return "ja";
    default: return "en";
  }
}

const char* LangNativeName(Lang l) {
  switch (l) {
    case Lang::kEs: return "Español";
    case Lang::kZhTW: return "繁體中文";
    case Lang::kJa: return "日本語";
    default: return "English";
  }
}

Lang NextLang(Lang l) {
  return static_cast<Lang>((static_cast<int>(l) + 1) % static_cast<int>(Lang::kCount));
}

bool IsCjk(Lang l) { return l == Lang::kZhTW || l == Lang::kJa; }

const char* Tr(Lang lang, std::string_view key) {
  const auto& t = Table();
  auto it = t.find(key);
  if (it == t.end()) return key.data();
  const char* s = it->second[static_cast<int>(lang)];
  return s && *s ? s : it->second[0];
}

std::string Fmt(std::string_view pattern, std::initializer_list<std::string> args) {
  std::string out;
  out.reserve(pattern.size() + 16);
  for (size_t i = 0; i < pattern.size(); ++i) {
    if (pattern[i] == '{' && i + 2 < pattern.size() && pattern[i + 2] == '}' &&
        std::isdigit(static_cast<unsigned char>(pattern[i + 1]))) {
      const size_t idx = pattern[i + 1] - '0';
      if (idx < args.size()) out += *(args.begin() + idx);
      i += 2;
    } else {
      out += pattern[i];
    }
  }
  return out;
}

std::string Localizer::RegionName(const geo::Region& r) const {
  const std::string* name = &r.name_en;
  switch (lang_) {
    case Lang::kEs: name = &r.name_es; break;
    case Lang::kZhTW: name = &r.name_zh; break;
    case Lang::kJa: name = &r.name_ja; break;
    default: break;
  }
  return name->empty() ? r.name_en : *name;
}

std::string Localizer::StateName(const election::StateInfo& s) const {
  switch (lang_) {
    case Lang::kEs: return s.name_es.empty() ? s.name : s.name_es;
    case Lang::kZhTW: return s.name_zh.empty() ? s.name : s.name_zh;
    case Lang::kJa: return s.name_ja.empty() ? s.name : s.name_ja;
    default: return s.name;
  }
}

std::string Localizer::CandidateName(const election::Candidate& c) const { return c.name; }

std::string Localizer::CandidateAltName(const election::Candidate& c,
                                        const election::ElectionData& data) const {
  if (c.party == "OTH" && !c.party_label.empty()) return c.party_label;
  return PartyName(data.party(c.party));
}

std::string Localizer::PartyShort(const election::Party& p) const {
  switch (lang_) {
    case Lang::kEs: return p.short_es.empty() ? p.short_en : p.short_es;
    case Lang::kZhTW: return p.short_zh.empty() ? p.short_en : p.short_zh;
    case Lang::kJa: return p.short_ja.empty() ? p.short_en : p.short_ja;
    default: return p.short_en.empty() ? p.code : p.short_en;
  }
}

std::string Localizer::PartyName(const election::Party& p) const {
  switch (lang_) {
    case Lang::kEs: return p.name_es.empty() ? p.name_en : p.name_es;
    case Lang::kZhTW: return p.name_zh.empty() ? p.name_en : p.name_zh;
    case Lang::kJa: return p.name_ja.empty() ? p.name_en : p.name_ja;
    default: return p.name_en;
  }
}

std::string Localizer::RaceTitle(const election::Race& r) const {
  std::string state = r.state_name;
  switch (lang_) {
    case Lang::kEs: state = r.state_name_es.empty() ? state : r.state_name_es; break;
    case Lang::kZhTW: state = r.state_name_zh.empty() ? state : r.state_name_zh; break;
    case Lang::kJa: state = r.state_name_ja.empty() ? state : r.state_name_ja; break;
    default: break;
  }
  const char* key = r.office == election::kGovernor ? "race.governor"
                    : r.special                     ? "race.senate_special"
                                                    : "race.senate";
  return Fmt(T(key), {state});
}

std::string Localizer::OfficeName(std::string_view office) const {
  if (office == election::kGovernor) return T("office.governor");
  if (office == election::kHouse) return T("office.house");
  return T("office.senate");
}

std::string Localizer::ListSeparator() const { return IsCjk(lang_) ? "、" : ", "; }

}  // namespace usa::ui
