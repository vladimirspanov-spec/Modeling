#!/usr/bin/env python3
"""Validate the calendar-v7 experiment and generate LaTeX from observations."""
import hashlib
import json
import math
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
DATA = ROOT / 'documentation/experiments/results-v7/comparison.json'
data = json.loads(DATA.read_text(encoding='utf-8'))
assert data['generator'] == 'calendar-v7', 'Current report requires calendar-v7'
assert data['complete'] is True, 'A partial series cannot be reported as complete'
runs = data['runs']
assert len(runs) == data['requestedRuns'] == 120
assert len(data['aggregates']) == 6
assert all(r['configuration']['dayCount'] == 14 and
           r['configuration']['departmentCount'] == 6 and
           r['configuration']['employeesPerDepartment'] == 8 and
           r['configuration']['stepMinutes'] == 30 and
           r['configuration']['initialMeetings'] and
           r['configuration']['periodicSeminars'] for r in runs)
rows = []
for aggregate in data['aggregates']:
    observations = [r for r in runs if r['case'] == aggregate['case']]
    assert len(observations) == aggregate['count'] == 20
    assert sorted(r['seed'] for r in observations) == list(range(42, 62))
    for r in observations:
        m = r['metrics']
        assert m['requests'] == sum(m[k] for k in ('accepted', 'rejected', 'pending', 'selected'))
        assert 0 <= m['roomUtilization'] <= 1
        assert m['completed'] <= m['started'] <= m['created']
        assert m['emailsDelivered'] == m['emailsSent']
    for key, mean in aggregate['mean'].items():
        values = [r['metrics'][key] for r in observations]
        assert math.isclose(mean, statistics.mean(values), rel_tol=1e-12, abs_tol=1e-12)
        assert math.isclose(aggregate['sampleStdDev'][key], statistics.stdev(values), rel_tol=1e-12, abs_tol=1e-12)
    policy = observations[0]['configuration']['conflictPolicy']
    rate = observations[0]['configuration']['requestRatePerWorkday']
    rows.append((policy, rate, aggregate['mean'], aggregate['sampleStdDev']))
assert {(p, r) for p, r, _, _ in rows} == {(p, r) for p in range(3) for r in (6, 12)}
rows.sort(key=lambda r: (r[1], r[0]))

def value(mean, sd, key, factor=1):
    return f"${factor*mean[key]:.2f}\\pm{factor*sd[key]:.2f}$"

table = [r'\begin{table}[H]\centering\small',
         r'\setlength{\tabcolsep}{3pt}',
         r'\begin{tabular}{crrrr}', r'\toprule',
         r'Режим & $\lambda$ & Завершено & Отказано & Занятость, \% \\', r'\midrule']
for policy, rate, mean, sd in rows:
    table.append(f"{'RSA'[policy]} & {rate:g} & {value(mean, sd, 'completed')} & "
                 f"{value(mean, sd, 'rejected')} & {value(mean, sd, 'roomUtilization', 100)} " + r'\\')
table += [r'\bottomrule', r'\end{tabular}',
          r'\caption{Результаты 120 прогонов calendar-v7: по 20 наблюдений на сочетание.}', r'\end{table}']
(OUT / 'results_table.tex').write_text('\n'.join(table) + '\n', encoding='utf-8')

text = []
for rate in (6, 12):
    selected = {p: (m, s) for p, r, m, s in rows if r == rate}
    reject, automatic = selected[0][0], selected[2][0]
    text.append(
        f"\\paragraph{{Интенсивность {rate:g}.}} "
        f"При отклонении конфликтов завершено в среднем {reject['completed']:.2f} мероприятий; "
        f"при автоматическом переносе --- {automatic['completed']:.2f}. "
        f"Разница составляет {automatic['completed']-reject['completed']:.2f} мероприятия. "
        f"Среднее число отказанных попыток соответственно {reject['rejected']:.2f} и {automatic['rejected']:.2f}; "
        f"имитируемых доставок --- {reject['emailsDelivered']:.2f} и {automatic['emailsDelivered']:.2f}."
    )
text.append(
    r'\paragraph{Наблюдение по стратегии предложений.} '
    'Результаты Suggest с выбором первого варианта близки к немедленному автоматическому переносу. '
    'Это согласуется с заданным программным правилом решения, но не доказывает равенство стратегий '
    'при другом поведении оператора. Большее число конфликтных заявок при переносах не означает '
    'ухудшение проверки: более заполненный календарь создаёт дополнительные пересечения, '
    'которые могут завершаться успешным переносом.'
)
(OUT / 'results_text.tex').write_text('\n\n'.join(text) + '\n', encoding='utf-8')
source_hashes = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in sorted((ROOT/'src/core').glob('*')) if p.is_file()}
manifest = {'source': str(DATA.relative_to(ROOT)),
            'source_sha256': hashlib.sha256(DATA.read_bytes()).hexdigest(),
            'generator': data['generator'], 'runs': len(runs),
            'seeds': sorted({r['seed'] for r in runs}),
            'validation': 'run balances, means and sample standard deviations recomputed',
            'core_source_sha256': source_hashes}
(OUT / 'data_manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(f'Validated {len(runs)} calendar-v7 runs; generated tables, text and manifest')
