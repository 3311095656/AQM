from pathlib import Path

from docx import Document
from docx.enum.table import WD_CELL_VERTICAL_ALIGNMENT, WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Pt, RGBColor

from generate_defense_qa_docx import (
    add_box,
    add_page_number,
    set_cell_margins,
    set_cell_shading,
    set_repeat_table_header,
    set_run_font,
)


OUTPUT = Path(__file__).with_name("AQMV2_初版问题复盘与修复说明.docx")


ISSUES = [
    {
        "rank": 1,
        "severity": "P0 / 阻断",
        "title": "固件与本地网页属于两条互不连通的链路",
        "initial": "初版固件连接OneNET的1883端口，并使用$sys物模型主题；本地Node服务则启动Aedes Broker监听8080端口，使用另一套不带$sys的本地主题。两者之间没有桥接，也没有统一API。",
        "risk": "真实传感器数据只能到OneNET，无法进入本地网页；本地网页发出的控制命令也无法到达固件。运行测试脚本时网页虽然有数据，但那只是本地模拟数据，不能证明真机端到端链路成功。",
        "root": "开发过程中同时保留了“设备直连OneNET”和“设备连接本地Broker”两种架构，但没有确定唯一数据流，也没有按完整的采集、上报、展示、控制、回复路径进行端到端验证。",
        "fix": "统一为“STM32固件直连OneNET MQTT，网页通过Node代理OneNET HTTPS API”。Node不再冒充设备连接MQTT，只负责托管页面、隐藏Token、查询属性、查询历史和下发控制。",
        "simple": "开发板把数据寄到OneNET，而旧网页只去电脑本地的另一个“邮局”取信，所以双方永远碰不到。后来让网页也通过OneNET这条正式通道取数据。",
        "oral": "初版最严重的问题是端云链路看起来都能单独运行，但实际上真机和本地网页没有贯通。我们重新画出完整数据流，取消冲突的本地MQTT设备方案，最终确定固件直连OneNET、Node只代理HTTPS API，使真实上报和网页控制使用同一条云端链路。",
    },
    {
        "rank": 2,
        "severity": "P0 / 安全",
        "title": "普通环境传感器故障会让燃气安全功能无法启动",
        "initial": "初版main函数先初始化AHT10、AP3216C和DHT22，任意一个初始化失败就直接return。MQ-5燃气线程位于这些初始化之后，因此普通温湿度或光照传感器故障时，整个主程序提前退出。",
        "risk": "一个非安全关键的普通环境传感器故障，可能连带导致蜂鸣器、燃气检测和安全联动完全无法运行，形成明显的故障扩散。",
        "root": "初始化流程没有区分安全关键模块与普通功能模块，采用了“全部成功才继续”的单一策略，缺少降级运行和故障隔离设计。",
        "fix": "将MQ-5安全线程提前启动；普通环境传感器失败时不再退出系统，而是暂停依赖这些数据的自动控制，保留燃气安全与其他正常功能，并每30秒重新尝试初始化。",
        "simple": "原来一个温度计坏了，燃气报警器也跟着不上班。修复后先保证报警器工作，温度计坏了只暂停相关功能，并定期重试。",
        "oral": "我们发现初版把所有传感器放在同一个成败条件中，导致普通传感器故障会拖垮安全模块。复赛版重新划分安全关键性，让MQ-5优先启动，普通环境传感器故障后进入降级模式而不是退出系统。",
    },
    {
        "rank": 3,
        "severity": "P0 / 控制正确性",
        "title": "空调自动控制误用了室外温度",
        "initial": "初版调用空调自动控制函数时传入out_temperature，即室外温度；但紧接着的日志打印的是in_temperature，即室内温度。",
        "risk": "室内空调可能根据室外环境错误动作，而且串口日志显示的是室内温度，会让调试人员误以为控制输入正确，增加问题定位难度。",
        "root": "室内外数据字段命名相近，调用参数与日志参数没有保持一致，也缺少针对控制输入来源的边界测试。",
        "fix": "将空调自动控制输入统一改为in_temperature，并保持日志、网页和控制算法使用同一室内温度字段。",
        "simple": "本来应该根据房间里面的温度开空调，程序却看了房间外面的温度，而且日志还显示成室内温度。",
        "oral": "初版有一个明确的控制变量错误：空调使用了室外温度，但日志打印室内温度。我们修正为统一使用室内温度，并把这类问题纳入变量来源和边界条件检查。",
    },
    {
        "rank": 4,
        "severity": "P0 / 安全仲裁",
        "title": "燃气报警时远程命令仍可能短暂覆盖安全动作",
        "initial": "初版主要依赖主循环每个周期重新强制开窗和开风扇，但MQTT回调收到远程命令后会直接操作执行器，没有在命令入口检查燃气状态。",
        "risk": "报警期间，远程命令可能短暂关闭风扇或窗户、关闭蜂鸣器，或者开启空调和湿控设备；主循环虽然会在下一周期纠正，但仍存在不安全时间窗口。",
        "root": "安全优先级只写在主循环的周期控制中，没有形成统一的命令仲裁入口，不同控制来源可以绕过安全策略直接访问执行器。",
        "fix": "在MQTT命令入口增加安全仲裁。报警期间拒绝关风扇、关窗、关闭蜂鸣器、开启空调或湿控以及切换自动模式，并返回403错误；主循环仍持续执行安全强制动作。",
        "simple": "原来网页可以先把安全设备关掉，系统过一会儿再打开。现在危险命令一进门就会被拒绝。",
        "oral": "初版虽然有燃气优先逻辑，但只靠主循环反复覆盖，不能阻止远程命令短暂执行。复赛版把安全判断前移到MQTT入口，不安全命令直接拒绝，形成入口拒绝加主循环强制的双层保护。",
    },
    {
        "rank": 5,
        "severity": "P1 / 内存安全",
        "title": "MQTT回调存在潜在越界写",
        "initial": "初版为了把MQTT payload当作C字符串解析，直接在payload[payloadlen]位置写入字符串结束符，但没有证明底层缓冲区在有效数据后额外预留了一个可写字节。",
        "risk": "可能破坏相邻内存，引起JSON解析异常、随机状态错误甚至系统崩溃；这种问题通常难以稳定复现。",
        "root": "把通信库提供的只读数据缓冲区当成了自有可写字符串，没有建立明确的长度和内存所有权边界。",
        "fix": "先检查消息长度，再复制到独立本地缓冲区并补结束符。控制消息超长时返回413，JSON无法解析时返回400，避免继续执行。",
        "simple": "收到一封信后，程序原来在别人信封外面多写了一个字，可能写到其他数据上。修复后先把内容复制到自己的纸上再处理。",
        "oral": "初版MQTT解析直接修改底层payload末尾，存在潜在越界。我们改为长度检查和独立缓冲区复制，并为超长载荷和非法JSON返回明确错误码。",
    },
    {
        "rank": 6,
        "severity": "P1 / 协议可靠性",
        "title": "错误命令多数仍回复成功，遥测采用QoS0",
        "initial": "初版对非法模式、错误类型、缺失参数和计划解析失败缺少严格校验，很多情况下最后仍统一回复200 success。属性上报和控制回复使用QoS0，发送接口失败后上层也难以获得明确结果。",
        "risk": "网页和云平台可能认为命令已经执行，实际设备却忽略或错误处理；网络抖动时重要状态和回复也缺少Broker确认。",
        "root": "只完成了正常数据格式下的功能路径，没有把错误输入、业务错误码和消息可靠性作为完整协议的一部分设计。",
        "fix": "增加JSON对象、数字类型、布尔值和长度校验；非法参数返回400，不安全命令返回403，超长返回413；属性发布和回复升级为QoS1，网页检查OneNET业务返回码。",
        "simple": "原来不管命令对不对，设备大多回答“成功”。现在会说明是参数错、太长，还是因为安全原因被拒绝。",
        "oral": "初版协议实现偏重正常流程，对错误输入仍可能回复成功。复赛版补充类型、长度和安全校验，使用差异化错误码，并把发布升级为QoS1，使通信结果更可解释。",
    },
    {
        "rank": 7,
        "severity": "P1 / 故障恢复",
        "title": "软件看门狗无法识别单线程失活",
        "initial": "初版使用8秒RT-Thread软件定时器作为看门狗，主线程和网络线程都可以重新启动同一个定时器。",
        "risk": "一个关键线程已经卡死时，另一个仍运行的线程可能继续喂狗，导致故障被掩盖；调度器、中断或软件定时器线程本身卡死时，软件看门狗也可能无法执行复位。",
        "root": "把“系统中还有线程运行”误当成“所有关键线程都健康”，而且软件看门狗没有独立于操作系统运行。",
        "fix": "当前作为竞赛原型已知边界保留。工程化方案是每个关键线程维护独立心跳，由监督线程检查全部心跳后喂STM32硬件IWDG，并在复位后记录故障原因。",
        "simple": "现在只要还有一个人在打卡，系统就以为所有人都正常。改进后要逐个点名，并由独立硬件负责超时复位。",
        "oral": "初版软件看门狗只能覆盖部分故障，不能证明单线程和系统级卡死都能恢复。我们把它明确列为原型边界，后续采用线程心跳汇总加硬件IWDG。",
    },
    {
        "rank": 8,
        "severity": "P1 / 状态管理",
        "title": "定时计划启停和退出恢复逻辑不完整",
        "initial": "初版虽然解析了单个时段的enabled字段，但检查时没有使用；离开时段后会统一切为自动，整体禁用计划时又可能保留之前被强制的模式。计划配置只存在RAM。",
        "risk": "用户关闭某个时段后设备仍可能执行；定时结束后用户原来的手动状态被覆盖；断电后计划全部丢失。",
        "root": "计划模块只实现了“当前时间是否匹配”，没有把进入前状态、退出事件、整体禁用和持久化作为完整状态机设计。",
        "fix": "检查时跳过禁用时段；进入计划前保存常用模式，退出或禁用时恢复；明确复杂混合状态仍是后续边界。计划持久化规划使用板载8MB NOR Flash与FAL/EasyFlash实现。",
        "simple": "原来定时任务结束后忘了用户之前怎么设置。现在开始前会记住常用状态，结束后再恢复。",
        "oral": "初版计划功能只关注时间匹配，忽略了时段禁用和状态恢复。复赛版补充enabled判断和进入退出状态保存；断电持久化仍作为下一阶段工作。",
    },
    {
        "rank": 9,
        "severity": "P1 / 凭据安全",
        "title": "Wi-Fi与OneNET凭据暴露，网页直接持有Token",
        "initial": "初版Wi-Fi账号、OneNET Token写在固件源码中，正式网页也直接包含设备Token；本地Broker和WebSocket没有认证与权限控制。",
        "risk": "拿到源码或网页的人可能获得设备访问权限；如果公开上传GitHub，历史版本中的凭据也可能继续泄露。",
        "root": "竞赛原型为了快速联调，把配置与代码写在一起，没有划分固件私有配置、服务端密钥和浏览器公开内容。",
        "fix": "浏览器不再保存Token，改为Node服务端代理OneNET API；设备详情接口过滤敏感字段。竞赛LAN环境仍使用静态凭据，公开发布前必须占位化并轮换Token，量产需TLS、安全存储和设备独立凭据。",
        "simple": "原来网页把设备钥匙直接带在身上，任何人打开网页都可能看到。现在钥匙由服务器保管，网页只提出请求。",
        "oral": "初版为了快速联调直接在浏览器和源码中保存Token。复赛版将网页权限收回到Node服务端并过滤敏感字段，同时明确静态凭据只是受控演示环境的取舍。",
    },
    {
        "rank": 10,
        "severity": "P1 / 数据有效性",
        "title": "传感器无效值与真实测量值没有清楚区分",
        "initial": "初版DHT22首次读取失败时可能使用默认0值，后续失败可能长期返回旧缓存；传感器数据结构没有统一的valid、故障码或最后更新时间字段。",
        "risk": "0值或过期值可能被误认为真实环境数据，参与控制、上传或历史统计，产生错误动作和错误结论。",
        "root": "数据模型只保存数值，没有把“数据是否有效、多久以前测得、为什么无效”作为同等重要的信息。",
        "fix": "初始化失败时清零结构体并暂停相关自动控制，系统每30秒重试传感器。完整方案仍应增加valid、故障码和时间戳，让页面显示“--”而不是把占位0当作真实值。",
        "simple": "“没有测到”与“真的测到0”不是一回事，系统必须明确区分。",
        "oral": "初版只关注数值，没有完整表达数据有效性。复赛版先保证无效数据不参与自动控制并增加重试；下一步会增加有效标志、故障码和数据年龄。",
    },
]


LESSONS = [
    "先画清楚唯一的端到端数据流，再分别开发固件、云端和网页。",
    "按照安全关键性划分模块，普通功能故障不能拖垮安全功能。",
    "安全优先级不仅要在周期控制中执行，还要在所有命令入口进行仲裁。",
    "通信协议必须同时设计正常路径、错误路径、长度边界和业务回复。",
    "传感器数据除了数值，还需要有效性、故障原因和时间信息。",
    "编译成功、网页有数据显示、Broker收到消息，都不能单独证明整机闭环成功。",
    "竞赛原型应明确区分已实现、部分实现、已知边界和后续规划。",
]


def build_document():
    doc = Document()
    section = doc.sections[0]
    section.page_width = Cm(21)
    section.page_height = Cm(29.7)
    section.top_margin = Cm(1.8)
    section.bottom_margin = Cm(1.7)
    section.left_margin = Cm(2.0)
    section.right_margin = Cm(2.0)

    normal = doc.styles["Normal"]
    normal.font.name = "Microsoft YaHei"
    normal._element.rPr.rFonts.set(qn("w:eastAsia"), "Microsoft YaHei")
    normal.font.size = Pt(10.5)
    normal.paragraph_format.line_spacing = 1.25
    normal.paragraph_format.space_after = Pt(5)

    for style_name, size, color in (
        ("Title", 28, (120, 40, 40)),
        ("Heading 1", 18, (120, 40, 40)),
        ("Heading 2", 13, (153, 61, 61)),
    ):
        style = doc.styles[style_name]
        style.font.name = "Microsoft YaHei"
        style._element.rPr.rFonts.set(qn("w:eastAsia"), "Microsoft YaHei")
        style.font.size = Pt(size)
        style.font.bold = True
        style.font.color.rgb = RGBColor(*color)
        style.paragraph_format.keep_with_next = True

    header = section.header.paragraphs[0]
    header.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    run = header.add_run("AQMV2 初版问题复盘  |  从功能演示到工程闭环")
    set_run_font(run, size=8.5, color=(120, 120, 120))
    add_page_number(section.footer.paragraphs[0])

    settings = doc.settings._element
    update_fields = OxmlElement("w:updateFields")
    update_fields.set(qn("w:val"), "true")
    settings.append(update_fields)

    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(58)
    r = p.add_run("AQMV2")
    set_run_font(r, size=34, bold=True, color=(120, 40, 40))

    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r = p.add_run("初版重大问题复盘与修复说明")
    set_run_font(r, size=24, bold=True, color=(153, 61, 61))

    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(10)
    r = p.add_run("初版现象 · 风险 · 根因 · 修复方案 · 小白解释 · 答辩口述")
    set_run_font(r, size=12, color=(110, 90, 90))

    cover = doc.add_table(rows=4, cols=2)
    cover.style = "Table Grid"
    cover.alignment = WD_TABLE_ALIGNMENT.CENTER
    values = [
        ("开发平台", "RT-Thread星火1号 / STM32F407"),
        ("复盘范围", "复赛版本集中修复前的初版"),
        ("重大问题", "10项"),
        ("整理日期", "2026-07-19"),
    ]
    for row, (left, right) in zip(cover.rows, values):
        row.cells[0].text = left
        row.cells[1].text = right
        set_cell_shading(row.cells[0], "F2DEDE")
        for cell in row.cells:
            set_cell_margins(cell)
            cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER
            for para in cell.paragraphs:
                for rr in para.runs:
                    set_run_font(rr, size=10.5, bold=cell is row.cells[0])

    doc.add_paragraph()
    add_box(
        doc,
        "复盘结论",
        "初版最大的问题不是功能少，而是系统边界没有统一：端云链路没有真实贯通，普通传感器故障会影响安全模块，远程命令也可能短暂覆盖燃气安全动作。复赛版本的核心工作是把“能演示”提升为“链路真实、行为可解释、异常可处理”。",
        "FDEDEC",
        (153, 45, 45),
    )
    doc.add_page_break()

    doc.add_heading("重大问题排序总览", level=1)
    table = doc.add_table(rows=1, cols=4)
    table.style = "Table Grid"
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    widths = ("排名", "问题", "等级", "修复状态")
    for idx, value in enumerate(widths):
        table.rows[0].cells[idx].text = value
        set_cell_shading(table.rows[0].cells[idx], "8C2F39")
        for para in table.rows[0].cells[idx].paragraphs:
            para.alignment = WD_ALIGN_PARAGRAPH.CENTER
            for rr in para.runs:
                set_run_font(rr, size=9.5, bold=True, color=(255, 255, 255))
    set_repeat_table_header(table.rows[0])
    for issue in ISSUES:
        cells = table.add_row().cells
        cells[0].text = str(issue["rank"])
        cells[1].text = issue["title"]
        cells[2].text = issue["severity"]
        cells[3].text = "已修复" if issue["rank"] not in (7, 10) else "已缓解/后续完善"
        if issue["rank"] <= 4:
            set_cell_shading(cells[2], "F8D7DA")
        else:
            set_cell_shading(cells[2], "FFF3CD")
        for cell in cells:
            set_cell_margins(cell)
            for para in cell.paragraphs:
                for rr in para.runs:
                    set_run_font(rr, size=9)
    doc.add_page_break()

    doc.add_heading("详细问题复盘", level=1)
    for issue in ISSUES:
        doc.add_heading(f"{issue['rank']}. {issue['title']}", level=2)
        p = doc.add_paragraph()
        r = p.add_run(f"严重等级：{issue['severity']}")
        set_run_font(r, size=9.5, bold=True, color=(183, 28, 28))
        add_box(doc, "初版现象", issue["initial"], "FFF7E6", (166, 95, 0))
        add_box(doc, "可能风险", issue["risk"], "FDEDEC", (183, 28, 28))
        add_box(doc, "根本原因", issue["root"], "F4F6F7", (69, 90, 100))
        add_box(doc, "解决办法", issue["fix"], "E8F5E9", (46, 125, 50))
        add_box(doc, "小白解释", issue["simple"], "E8F3F8", (17, 94, 130))
        add_box(doc, "答辩口述", issue["oral"], "F3E5F5", (106, 27, 154))
    doc.add_page_break()

    doc.add_heading("最值得在答辩中主动讲的三个问题", level=1)
    top_three = [ISSUES[0], ISSUES[1], ISSUES[3]]
    for issue in top_three:
        doc.add_heading(f"{issue['rank']}. {issue['title']}", level=2)
        p = doc.add_paragraph(issue["oral"])
        p.paragraph_format.line_spacing = 1.3

    doc.add_heading("开发过程总结口述稿", level=1)
    add_box(
        doc,
        "建议口述",
        "初版最大的问题不是功能数量不足，而是系统边界没有统一：真机和网页链路没有贯通，普通传感器故障会影响燃气安全，远程命令也可能短暂覆盖安全动作。进入复赛后，我们没有继续盲目堆叠功能，而是重新梳理端到端数据流，隔离安全关键任务，在命令入口增加安全仲裁，并完善MQTT输入校验、错误回复和传感器故障降级，使系统从“功能可以演示”提升到“行为能够解释、异常能够处理”。",
        "F3E5F5",
        (106, 27, 154),
    )

    doc.add_heading("工程经验总结", level=1)
    for lesson in LESSONS:
        p = doc.add_paragraph(style="List Bullet")
        r = p.add_run(lesson)
        set_run_font(r, size=10.5)

    doc.core_properties.title = "AQMV2初版重大问题复盘与修复说明"
    doc.core_properties.subject = "嵌入式竞赛复赛版本问题复盘"
    doc.core_properties.author = "AQMV2 Team"
    doc.core_properties.keywords = "AQMV2, 初版问题, 修复, RT-Thread, STM32F407, OneNET"
    doc.save(OUTPUT)


if __name__ == "__main__":
    build_document()
    print(OUTPUT)
