//! Import of ground-truth expert traces written as CSV by
//! `tools/moe_trace` (one row per selected expert):
//!
//! ```text
//! token,layer,expert,t_ns
//! 0,0,17,1203344
//! ```
//!
//! The header row is optional. Blank lines and lines starting with `#` are
//! ignored. Rows are returned sorted by (token, layer, t_ns, expert).

use crate::ExpertEvent;

#[derive(Debug, thiserror::Error, PartialEq, Eq)]
#[error("line {line}: {msg}")]
pub struct CsvError {
    pub line: usize,
    pub msg: String,
}

/// Parse expert-trace CSV. `n_layers`/`n_experts`, when given, bound-check rows.
pub fn parse_expert_csv(
    text: &str,
    n_layers: Option<u32>,
    n_experts: Option<u32>,
) -> Result<Vec<ExpertEvent>, CsvError> {
    let mut out = Vec::new();
    for (i, raw) in text.lines().enumerate() {
        let line = i + 1;
        let l = raw.trim();
        if l.is_empty() || l.starts_with('#') || (out.is_empty() && l.starts_with("token")) {
            continue;
        }
        let f: Vec<&str> = l.split(',').map(str::trim).collect();
        if f.len() != 4 {
            return Err(CsvError {
                line,
                msg: format!("expected 4 fields, got {}", f.len()),
            });
        }
        let num = |s: &str, what: &str| -> Result<u64, CsvError> {
            s.parse::<u64>().map_err(|_| CsvError {
                line,
                msg: format!("bad {what} {s:?}"),
            })
        };
        let (token, layer, expert, t_ns) = (
            num(f[0], "token")?,
            num(f[1], "layer")?,
            num(f[2], "expert")?,
            num(f[3], "t_ns")?,
        );
        if token > u32::MAX as u64 || layer > u16::MAX as u64 || expert > u16::MAX as u64 {
            return Err(CsvError {
                line,
                msg: "value out of range".into(),
            });
        }
        if n_layers.is_some_and(|n| layer >= n as u64) {
            return Err(CsvError {
                line,
                msg: format!("layer {layer} >= n_layers"),
            });
        }
        if n_experts.is_some_and(|n| expert >= n as u64) {
            return Err(CsvError {
                line,
                msg: format!("expert {expert} >= n_experts"),
            });
        }
        out.push(ExpertEvent {
            t_ns,
            token: token as u32,
            layer: layer as u16,
            expert: expert as u16,
        });
    }
    out.sort_by_key(|e| (e.token, e.layer, e.t_ns, e.expert));
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_with_and_without_header() {
        let a = parse_expert_csv(
            "token,layer,expert,t_ns\n1,0,3,50\n0,1,2,20\n0,0,5,10\n",
            None,
            None,
        )
        .unwrap();
        assert_eq!(a.len(), 3);
        assert_eq!((a[0].token, a[0].layer, a[0].expert), (0, 0, 5));
        assert_eq!((a[2].token, a[2].layer), (1, 0));
        let b = parse_expert_csv("# comment\n\n0,0,5,10\n", Some(1), Some(8)).unwrap();
        assert_eq!(b.len(), 1);
    }

    #[test]
    fn rejects_bad_rows() {
        assert_eq!(parse_expert_csv("0,0,5\n", None, None).unwrap_err().line, 1);
        assert!(parse_expert_csv("0,x,5,1\n", None, None).is_err());
        assert!(parse_expert_csv("0,2,5,1\n", Some(2), None).is_err());
        assert!(parse_expert_csv("0,0,64,1\n", None, Some(64)).is_err());
        assert!(parse_expert_csv("0,70000,1,1\n", None, None).is_err());
    }
}
