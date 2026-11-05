#!/usr/bin/env python3
"""Build LaTeX text from actual experiment JSON (no external dependencies)."""
import json
import math
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
DATA = ROOT / 'documentation/experiments/results/comparison.json'
data = json.loads(DATA.read_text())
assert data['complete'], 'Report requires a completed experiment'
runs = data['runs']
assert len(runs) == data['requestedRuns'] == 120, 'This report describes the fixed 120-run design'
assert all(r['configuration']['dayCount'] == 14 and r['configuration']['departmentCount'] == 6 and r['configuration']['employeesPerDepartment'] == 8 for r in runs)
assert all(a['count'] == 20 for a in data['aggregates'])
rows = []
for index, aggregate in enumerate(data['aggregates']):
    observations = [r for r in runs if r['case'] == aggregate['case']]
    assert len(observations) == aggregate['count'] and len(observations) > 1
    for r in observations:
        m = r['metrics']
        assert m['requests'] == sum(m[k] for k in ['accepted', 'rejected', 'pending', 'selected'])
        assert 0 <= m['roomUtilization'] <= 1
        assert m['completed'] <= m['started'] <= m['created']
    for key, mean in aggregate['mean'].items():
        values = [r['metrics'][key] for r in observations]
        assert math.isclose(mean, statistics.mean(values), rel_tol=1e-12, abs_tol=1e-12)
        assert math.isclose(aggregate['sampleStdDev'][key], statistics.stdev(values), rel_tol=1e-12, abs_tol=1e-12)
    strategy = ['Отклонение заявки', 'Предложение вариантов', 'Автоматический перенос'][index % 3]
    rate = observations[0]['configuration']['requestRatePerWorkday']
    label = f'«{strategy}», интенсивность {rate:g}'
    rows.append((label, aggregate['mean'], aggregate['sampleStdDev']))

paragraphs = []
for label, mean, sd in rows:
    paragraphs.append(
        f"\\paragraph{{{label}.}} "
        f"Выполнено 20 прогонов. Среднее число созданных мероприятий — {mean['created']:.2f}, "
        f"завершённых — {mean['completed']:.2f} (выборочное стандартное отклонение {sd['completed']:.2f}), "
        f"отказанных попыток — {mean['rejected']:.2f} (отклонение {sd['rejected']:.2f}). "
        f"Среднее число заявок — {mean['requests']:.2f}, конфликтных — {mean['conflicts']:.2f}. "
        f"Доставлено в среднем {mean['emailsDelivered']:.2f} имитируемых писем. "
        f"Занятость помещений — {100*mean['roomUtilization']:.2f}\\% "
        f"(стандартное отклонение {100*sd['roomUtilization']:.2f} процентного пункта)."
    )
base, automatic = rows[3][1], rows[5][1]
paragraphs.append(
    f"\\paragraph{{Сравнение при интенсивности 12.}} "
    f"Среднее число завершённых мероприятий увеличилось с {base['completed']:.2f} "
    f"при стратегии «Отклонение заявки» до {automatic['completed']:.2f} при стратегии «Автоматический перенос» "
    f"(на {automatic['completed']-base['completed']:.2f}, или "
    f"{100*(automatic['completed']/base['completed']-1):.1f}\\%). "
    f"Число отказанных попыток снизилось с {base['rejected']:.2f} до {automatic['rejected']:.2f}. "
    "Эти различия относятся к выполненным запускам; статистическая значимость и причинный эффект не заявляются."
)
(OUT / 'results_text.tex').write_text('\n\n'.join(paragraphs) + '\n')
(OUT / 'data_manifest.json').write_text(json.dumps({'source': str(DATA.relative_to(ROOT)),
    'generator': data['generator'], 'runs': len(runs), 'seeds': sorted({r['seed'] for r in runs}),
    'validation': 'all run balances, means and sample standard deviations recomputed'}, ensure_ascii=False, indent=2) + '\n')
print(f'Validated {len(runs)} runs; generated result descriptions in {OUT}')
