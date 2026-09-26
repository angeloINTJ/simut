// rules.js — as regras do manifesto, do jeito que a página as aplica.
//
// A mesma semântica de tools/gen_features.py rule_violations( ): uma regra é
// quebrada quando TODAS as condições de `when` valem e alguma de `require` não.
// A página só usa isto para travar uma opção e mostrar o motivo; quem decide é o
// CI — tools/build_custom.py reaplica as regras antes de compilar. Para as duas
// não divergirem, tools/test_configurator_rules.py roda esta função e a do
// Python contra a mesma tabela (tools/configurator_cases.json).

export function matches(config, conds) {
  return Object.entries(conds).every(([key, value]) => config[key] === value);
}

// As regras que `config` quebra, na ordem do manifesto.
export function ruleViolations(model, config) {
  return model.rules.filter((r) => matches(config, r.when) && !matches(config, r.require));
}

// Os avisos que `config` acende: compila, mas carrega um risco conhecido.
export function hazardHits(model, config) {
  return model.hazards.filter((h) => matches(config, h.when));
}
