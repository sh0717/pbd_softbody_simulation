"""README의 구현 개념도를 PNG로 생성합니다. Python 3 + Pillow가 필요합니다."""

import argparse
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "docs/images/algorithms"
WIDTH = 1600
INK = "#172b42"
MUTED = "#53677e"
BLUE = "#2463b4"
TEAL = "#007c79"
ORANGE = "#bd5b14"
PALE_BLUE = "#edf4fe"
PALE_TEAL = "#e8f7f3"
PALE_ORANGE = "#fff2e7"
LINE = "#cfdae6"


def font(size, bold=False):
    return ImageFont.truetype(str(FONT_BOLD if bold else FONT), size)


def label(x, y, value, size=26, color=INK, bold=False, anchor=None):
    DRAW.text((x, y), value, font=font(size, bold), fill=color, anchor=anchor)


def arrow(start, end, color=BLUE, width=5):
    DRAW.line([start, end], fill=color, width=width)
    angle = math.atan2(end[1] - start[1], end[0] - start[0])
    head = [end]

    for offset in (-0.48, 0.48):
        head.append((end[0] - 18 * math.cos(angle + offset), end[1] - 18 * math.sin(angle + offset)))

    DRAW.polygon(head, fill=color)


def box(bounds, title, lines=(), fill=PALE_BLUE, accent=BLUE, title_size=29):
    x0, y0, x1, y1 = bounds
    DRAW.rounded_rectangle(bounds, radius=18, fill=fill, outline=LINE, width=2)
    DRAW.rounded_rectangle((x0, y0, x0 + 8, y1), radius=4, fill=accent)
    label(x0 + 25, y0 + 19, title, title_size, accent, True)

    for index, line in enumerate(lines):
        label(x0 + 25, y0 + 65 + index * 37, line, 25)


def canvas(title, subtitle, height):
    global IMAGE, DRAW
    IMAGE = Image.new("RGB", (WIDTH, height), "#ffffff")
    DRAW = ImageDraw.Draw(IMAGE)
    DRAW.rectangle((0, 0, WIDTH, 12), fill=BLUE)
    label(65, 40, title, 43, bold=True)
    label(67, 103, subtitle, 25, MUTED)
    DRAW.line((65, height - 65, WIDTH - 65, height - 65), fill=LINE, width=2)
    label(67, height - 48, "Soft / Lab  |  현재 소스의 처리 순서를 재구성한 개념도 · 런타임 캡처 아님", 22, MUTED)


def save(name):
    path = OUTPUT / name
    IMAGE.save(path, optimize=True)
    print(path.relative_to(ROOT))


def pipeline():
    canvas("01  전체 물리 알고리즘", "렌더링 프레임 → 고정 dt → S개 서브스텝 → 각 서브스텝에서 I회 제약 반복", 1130)
    box((65, 165, 1535, 280), "앱: 경과 시간을 누적하고 고정 dt만큼 Step 호출", ["requestAnimationFrame → advancePhysics → PhysicsWorld.step → FPhysicsWorld::Step"])
    arrow((800, 282), (800, 320))
    DRAW.rounded_rectangle((65, 325, 1535, 1020), radius=22, fill="#f8fafc", outline=LINE, width=3)
    label(90, 343, "S회 반복: h = dt / S", 28, BLUE, True)
    box((100, 395, 1500, 515), "① 모든 몸체의 위치 예측", ["이전 위치 저장 → λ=0 → 중력·감쇠 / 드래그 → 위치 갱신 → 입사 속도 저장"])
    arrow((800, 518), (800, 550))
    box((100, 555, 1500, 770), "② 제약 반복 I회", ["몸체별 거리 → 체적 제약 (몸체 사이 병렬 실행 가능)", "모든 제약 작업 완료 대기 → 동적 충돌: 구 관련 → 연체 쌍 접촉", "정적 충돌: 월드 경계 → Box 장애물"], PALE_TEAL, TEAL)
    arrow((800, 773), (800, 807), TEAL)
    box((100, 812, 1500, 955), "③ 속도 복원 → 반발 → 범퍼 → 시뮬레이션 시간 +h", ["v = (보정 위치 - 이전 위치) / h", "모든 몸체가 같은 서브스텝을 마친 뒤 다음 h로 진행"], PALE_ORANGE, ORANGE)
    label(100, 978, "Step 종료 후: Wasm 위치 재조회 → Three.js 위치·법선 갱신 → 화면 출력", 25, MUTED)
    save("01-pipeline.png")


def dot(point, name, color=BLUE, offset=(15, 8)):
    x, y = point
    DRAW.ellipse((x - 10, y - 10, x + 10, y + 10), fill=color)
    label(x + offset[0], y + offset[1], name, 26, color, True)


def tetrahedron(points, color=BLUE):
    for first, second in ((0, 1), (0, 2), (0, 3), (1, 2), (1, 3), (2, 3)):
        DRAW.line((points[first], points[second]), fill=color, width=4)

    for index, point in enumerate(points):
        dot(point, f"x{index}", color)


def model():
    canvas("02  메시를 입자와 제약으로 표현", "거리 제약은 변 길이를, 체적 제약은 사면체의 부호 있는 체적을 복원합니다.", 880)
    box((65, 165, 755, 735), "물리 메시: 사면체 하나의 예", [])
    tetrahedron([(180, 520), (530, 575), (625, 405), (360, 295)])
    label(100, 640, "변: 두 입자   /   사면체: 네 입자", 27)
    label(100, 682, "표면 삼각형: 충돌 탐색과 렌더링에 사용", 25, MUTED)
    box((800, 165, 1535, 390), "저장하는 데이터", ["Positions: [x0,y0,z0, x1,y1,z1, ...]", "EdgeIndices / TetIndices / SurfaceIndices", "RestLengths / RestVolumes / InverseMass"])
    box((800, 420, 1535, 735), "역질량 w = 1 / m", ["w=0: 고정 입자, 보정량 0", "w가 클수록 같은 제약에서 더 많이 이동", "거리 오차 0.2, wA=1, wB=3인 PBD 예:", "A가 0.05, B가 0.15만큼 서로 접근"], PALE_TEAL, TEAL)
    save("02-particle-model.png")


def edge_row(y, left, right, description, color):
    DRAW.line((190, y, 1410, y), fill=LINE, width=3)
    x0 = 250 + left * 850
    x1 = 250 + right * 850
    DRAW.line((x0, y, x1, y), fill=color, width=7)
    dot((x0, y), f"A = {left:.2f}", color, (-10, 20))
    dot((x1, y), f"B = {right:.2f}", color, (-105, 20))
    label(105, y - 61, description, 27, color, True)


def xpbd():
    canvas("03  거리 제약: PBD와 XPBD", "한 변만 있는 1차원 예 · 기준 길이 1 · 역질량 wA=wB=1 · 접촉과 다른 제약 없음", 985)
    edge_row(255, 0, 1.2, "예측 위치: C = 1.2 - 1 = 0.2", MUTED)
    edge_row(455, 0.1, 1.1, "PBD, k=1: Δλ=-0.2/2=-0.1 → 길이 1", BLUE)
    edge_row(655, 0.05, 1.15, "XPBD, α/h²=2, λ=0: Δλ=-0.2/4=-0.05 → 길이 1.1", TEAL)
    box((65, 770, 1535, 890), "다음 반복: 누적 λ가 이전 보정을 반영", ["C=0.1, λ=-0.05 → -C - (α/h²)λ = -0.1 - 2(-0.05) = 0 → 추가 보정 0"], PALE_TEAL, TEAL)
    save("03-pbd-xpbd.png")


def volume():
    canvas("04  부호 있는 체적과 구현의 6배 스케일", "기준 사면체 x0=(0,0,0), x1=(1,0,0), x2=(0,1,0) · x3의 높이를 변경한 수치 예", 1000)
    origins = [95, 600, 1105]
    titles = ["기준: x3=(0,0,1)", "압축: x3=(0,0,0.5)", "뒤집힘: x3=(0,0,-1)"]
    values = ["V0 = 1/6", "V = 1/12", "V = -1/6"]

    for index, origin in enumerate(origins):
        color = ORANGE if index == 2 else BLUE
        box((origin - 30, 170, origin + 430, 655), titles[index], [], PALE_ORANGE if index == 2 else PALE_BLUE, color, 25)
        top_y = [290, 360, 555][index]
        tetrahedron([(origin + 70, 455), (origin + 305, 490), (origin + 345, 415), (origin + 180, top_y)], color)
        label(origin + 45, 595, values[index], 31, color, True)

    box((65, 690, 1535, 895), "현재 구현: C = 6(V - V0),  기울기도 6배", ["에너지 C²/(2α)를 유지하려면 유연도도 6²배: α_scaled = 36α", "따라서 체적 XPBD 분모에는 36 × VolumeCompliance / h²를 사용", "한 번의 최대 입자 이동을 제한한 뒤, 실제 적용한 Δλ만 누적"], PALE_TEAL, TEAL)
    save("04-signed-volume.png")


def contacts():
    canvas("05  연체 접촉: 후보 탐색과 위치 보정", "몸체 경계 → 사면체 셀·무게중심 포함 검사 → 표면 BVH 최근접점 → 양쪽 몸체 보정", 1110)
    box((65, 170, 515, 385), "① 몸체 쌍 후보", ["경계구 + AABB 겹침", "멀리 떨어진 쌍 제외", "A→B, B→A 양방향 검사"])
    box((575, 170, 1025, 385), "② 내부 입자 판정", ["셀에 등록된 사면체 후보", "사면체 무게중심 좌표 검사", "큰 사면체는 초과 목록 사용"])
    box((1085, 170, 1535, 385), "③ 가장 가까운 표면", ["표면 BVH를 현재 위치로 refit", "상자 거리 하한으로 가지치기", "삼각형 점 q와 가중치 β 계산"])
    arrow((520, 275), (570, 275))
    arrow((1030, 275), (1080, 275))
    DRAW.rounded_rectangle((65, 425, 780, 970), radius=20, fill="#f8fafc", outline=LINE, width=2)
    label(90, 448, "④ 내부의 p를 표면 바깥으로 이동", 28, BLUE, True)
    triangle = [(190, 650), (660, 675), (565, 525)]
    DRAW.polygon(triangle, fill="#dcebf7", outline=BLUE, width=4)

    for index, point in enumerate(triangle):
        dot(point, f"b{index}", BLUE, (-5, -42))

    dot((430, 625), "q = Σ βj bj", TEAL, (20, -10))
    dot((430, 785), "p: 침투 입자", ORANGE, (20, -10))
    arrow((430, 770), (430, 580), TEAL)
    label(220, 710, "법선 n", 27, TEAL)
    arrow((580, 690), (580, 765), BLUE)
    label(95, 845, "p는 +n, 대상 정점은 -n 방향 보정", 25)
    label(95, 892, "β는 접촉 생성 시 계산하여 보관", 25, MUTED)
    box((825, 425, 1535, 690), "분리 제약: C = (p - q)·n - margin", ["침투 상태 C < 0일 때만 보정", "D = wp + Σ(wj × βj²)", "Δλ = -C / D", "Δp = wp × n × Δλ"], PALE_TEAL, TEAL, 26)
    box((825, 725, 1535, 970), "가중치가 제곱되는 이유", ["대상 정점 보정에 βj가 한 번 곱해짐", "보정된 q를 보간할 때 다시 βj가 곱해짐", "모든 쌍의 접촉 수집 후 순서대로 투영", "한 몸체의 자체 충돌은 처리하지 않음"], PALE_ORANGE, ORANGE)
    save("05-soft-contact.png")


def parallel():
    canvas("06  병렬 작업 경계와 렌더링 데이터 흐름", "같은 몸체 내부는 순차 · 서로 다른 몸체의 재질 제약만 병렬 · Step 호출은 동기식", 1070)

    for index, name in enumerate(("몸체 A", "몸체 B", "몸체 C")):
        x = 65 + index * 505
        box((x, 175, x + 460, 355), name + ": 독립 위치·λ 배열", ["SolveEdges", "SolveVolumes"], PALE_TEAL, TEAL, 25)
        arrow((x + 230, 358), (x + 230, 415), TEAL)

    box((65, 420, 1535, 545), "FParallelExecutor::Run 반환 = 모든 작업 완료 + 쓰기 가시성", ["호출 스레드도 작업 수행 · 작업자 풀 재사용 · 몸체 간 공유 접촉은 완료 대기 이후 처리"])
    arrow((800, 548), (800, 590))
    box((65, 595, 1535, 725), "호출 스레드: 동적 충돌 → 정적 충돌 → 속도·반발·범퍼", ["C++ world가 상태 소유 → Embind가 메모리 뷰 대여 → JS가 현재 뷰를 다시 취득"])
    arrow((800, 728), (800, 765))
    box((65, 770, 1535, 895), "렌더러: Float64 물리 위치 → Float32 BufferAttribute → 법선 갱신 → WebGL", ["Wasm 메모리가 증가하면 이전 JS 뷰가 유효하지 않을 수 있으므로 조회 때 다시 취득"], PALE_ORANGE, ORANGE)
    label(70, 934, "격리 HTTP + SharedArrayBuffer: 병렬 코어   |   file:// / 비격리 HTTP: 순차 코어", 27, MUTED)
    save("06-parallel-rendering.png")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", type=Path, default=Path("C:/Windows/Fonts/malgun.ttf"), help="한글 TrueType 폰트 경로")
    parser.add_argument("--bold-font", type=Path, default=Path("C:/Windows/Fonts/malgunbd.ttf"), help="한글 굵은 TrueType 폰트 경로")
    arguments = parser.parse_args()
    FONT = arguments.font
    FONT_BOLD = arguments.bold_font

    if not FONT.is_file() or not FONT_BOLD.is_file():
        parser.error("--font와 --bold-font로 설치된 한글 폰트를 지정합니다.")

    OUTPUT.mkdir(parents=True, exist_ok=True)

    for generate in (pipeline, model, xpbd, volume, contacts, parallel):
        generate()
