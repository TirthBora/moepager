//! `import-csv`: ground-truth expert trace (from `tools/moe_trace`) → `.mpt`.

use std::path::PathBuf;

use anyhow::{bail, Context, Result};
use mp_trace::{Observation, TraceHeader};

use crate::common::load_map;

#[derive(clap::Args)]
pub struct Args {
    /// CSV with rows `token,layer,expert,t_ns`.
    pub csv: PathBuf,
    #[arg(short, long)]
    pub out: PathBuf,
    /// Expert map of the model the trace came from (shape and bounds checks).
    #[arg(long)]
    pub map: Option<PathBuf>,
    /// Shape when no map is given.
    #[arg(long)]
    pub layers: Option<u32>,
    #[arg(long)]
    pub experts: Option<u32>,
    #[arg(long)]
    pub top_k: Option<u32>,
    /// Free-text provenance stored in the header (model, prompt, llama.cpp commit…).
    #[arg(long)]
    pub note: Option<String>,
}

pub fn run(a: Args) -> Result<()> {
    let map = a.map.as_deref().map(load_map).transpose()?;
    let (nl, ne, k) = match &map {
        Some(m) => (m.n_layers, m.n_experts, m.top_k),
        None => match (a.layers, a.experts) {
            (Some(l), Some(e)) => (l, e, a.top_k),
            _ => bail!("give --map, or both --layers and --experts"),
        },
    };
    let text =
        std::fs::read_to_string(&a.csv).with_context(|| format!("reading {}", a.csv.display()))?;
    let ev = mp_trace::parse_expert_csv(&text, Some(nl), Some(ne))?;
    if ev.is_empty() {
        bail!("{} contains no expert rows", a.csv.display());
    }
    let mut h = TraceHeader::new("llama-evalcb", nl, ne);
    h.observation = Observation::Full;
    h.top_k = k;
    h.model_file = map
        .as_ref()
        .and(a.map.as_ref())
        .map(|p| p.display().to_string());
    h.params = serde_json::json!({ "imported_from": a.csv.display().to_string(), "note": a.note });
    mp_trace::write_expert_file(&a.out, &h, &ev)?;
    let tokens = ev
        .iter()
        .map(|e| e.token)
        .collect::<std::collections::BTreeSet<_>>()
        .len();
    // Sanity: rows per (token, layer) should equal top_k.
    if let Some(k) = k {
        let mut per: std::collections::BTreeMap<(u32, u16), u32> = Default::default();
        for e in &ev {
            *per.entry((e.token, e.layer)).or_default() += 1;
        }
        let bad = per.values().filter(|&&c| c != k).count();
        if bad > 0 {
            eprintln!("warning: {bad} (token, layer) groups do not have exactly top_k={k} experts");
        }
    }
    println!(
        "imported {} rows, {} tokens -> {}",
        ev.len(),
        tokens,
        a.out.display()
    );
    Ok(())
}
