import argparse
import json
import math
from pathlib import Path

import pygame


def draw_ship(surf, color, x, y, r, heading):
    w, h = surf.get_size()
    x0 = int(x * w)
    y0 = int(y * h)

    theta = math.radians(90.0 - heading)
    ang120 = 2.0 * math.pi / 3.0

    rt = 0.9 * r

    for dx in (-w, 0, w):
        for dy in (-h, 0, h):
            cx = x0 + dx
            cy = y0 + dy
            if -r <= cx <= w + r and -r <= cy <= h + r:
                P = cx, h - cy

                Ax = cx + rt * math.cos(theta)
                Ay = cy + rt * math.sin(theta)
                Bx = cx + rt * math.cos(theta + ang120)
                By = cy + rt * math.sin(theta + ang120)
                Cx = cx + rt * math.cos(theta - ang120)
                Cy = cy + rt * math.sin(theta - ang120)

                A = int(round(Ax)), h - int(round(Ay))
                B = int(round(Bx)), h - int(round(By))
                C = int(round(Cx)), h - int(round(Cy))

                pygame.draw.polygon(surf, color, [A, B, P])
                pygame.draw.polygon(surf, color, [A, P, C])
                pygame.draw.circle(surf, color, P, r, width=2)


def draw_shot(surf, color, x, y, r):
    w, h = surf.get_size()
    x0 = int(x * w)
    y0 = int(y * h)

    for dx in (-w, 0, w):
        for dy in (-h, 0, h):
            cx = x0 + dx
            cy = y0 + dy
            if -r <= cx <= w + r and -r <= cy <= h + r:
                pygame.draw.circle(surf, color, (cx, h - cy), r)


def blit_overlay(screen, font, items, x=12, y=12, pad=8, line_gap=4):
    rendered = []

    max_width = 0
    total_height = 0

    for text, color in items:
        surf = font.render(text, True, color)
        rendered.append(surf)
        if surf.get_width() > max_width:
            max_width = surf.get_width()

        total_height += surf.get_height() + line_gap

    if rendered:
        total_height -= line_gap

    bkg = pygame.Surface((max_width + 2 * pad, total_height + 2 * pad), pygame.SRCALPHA)
    bkg.fill((0, 0, 0, 160))
    screen.blit(bkg, (x, y))

    cy = y + pad
    for surf in rendered:
        screen.blit(surf, (x + pad, cy))
        cy += surf.get_height() + line_gap


def main():
    parser = argparse.ArgumentParser(
        prog="scubywasm-show",
        description="Show a replay of a Scubywasm JSON log.",
    )
    parser.add_argument(
        "logfile",
        type=Path,
        metavar="LOGFILE",
        help="path to the JSON logfile",
    )
    args = parser.parse_args()

    data = json.loads(args.logfile.read_text())
    log = data["history"]

    n_teams = len(log)
    team_names = (
        data["teams"] if "teams" in data else [f"Team {i + 1}" for i in range(n_teams)]
    )
    team_colors = [
        (230, 80, 80),  # red
        (90, 160, 230),  # blue
        (140, 210, 110),  # green
        (200, 120, 220),  # purple
        (240, 170, 70),  # orange
        (90, 200, 170),  # teal
        (240, 220, 90),  # yellow
        (230, 120, 170),  # rose
        (140, 120, 230),  # indigo
        (200, 200, 200),  # gray
    ][:n_teams]

    ticks = int(data["ticks"])
    max_tick = max(0, ticks - 1)
    ship_hit_radius = float(data["ship_hit_radius"])

    pygame.init()
    try:
        width, height = 900, 900
        screen = pygame.display.set_mode((width, height))

        clock = pygame.time.Clock()

        font = pygame.font.Font(None, 22)

        r_ship = max(2, int(ship_hit_radius * min(width, height)))
        r_shot = 5

        t = 0.0
        running = True
        while running:
            dt = clock.tick(60) / 1_000

            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    running = False
                elif event.type == pygame.KEYDOWN:
                    if event.key in (pygame.K_ESCAPE, pygame.K_q):
                        running = False
                elif event.type == pygame.KEYUP:
                    if event.key == pygame.K_UP:
                        t = float(min(max_tick, t + 1))
                    elif event.key == pygame.K_DOWN:
                        t = float(max(0, t - 1))

            keys = pygame.key.get_pressed()
            if keys[pygame.K_RIGHT] and not keys[pygame.K_LEFT]:
                t = float(min(max_tick, t + 10 * dt))
            elif keys[pygame.K_LEFT] and not keys[pygame.K_RIGHT]:
                t = float(max(0, t - 10 * dt))

            t = float(max(0, min(max_tick, t)))
            tick = int(t)

            pygame.display.set_caption(
                f"Scubywasm replay {args.logfile} | Tick: {tick + 1}/{max_tick + 1}"
            )
            screen.fill((15, 15, 18))

            for i, team in enumerate(log):
                color = team_colors[i % len(team_colors)]

                for shot in team["shots"].values():
                    if shot["lifetime"][tick] > 0:
                        draw_shot(
                            screen,
                            color,
                            shot["x"][tick],
                            shot["y"][tick],
                            r_shot,
                        )

                for ship in team["ships"].values():
                    if ship["alive"][tick]:
                        draw_ship(
                            screen,
                            color,
                            ship["x"][tick],
                            ship["y"][tick],
                            r_ship,
                            ship["heading"][tick],
                        )

            color = (235, 235, 235)
            items = [
                ("Hold RIGHT/LEFT: +/- 10 ticks/s", color),
                ("Press DOWN/UP: +/- 1 tick", color),
                ("Press Q or ESC: quit", color),
                ("", color),
                ("SCORES", color),
            ]

            for name, color, history in zip(team_names, team_colors, log):
                items.append((f"  {name}: {history['scores'][tick]:+d}", color))

            blit_overlay(screen, font, items)

            pygame.display.flip()
    finally:
        pygame.quit()


if __name__ == "__main__":
    main()
