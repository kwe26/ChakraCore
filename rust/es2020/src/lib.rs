use serde::Serialize;
use swc_common::{sync::Lrc, FileName, SourceMap};
use swc_ecma_ast::{BigInt, BinExpr, BinaryOp, OptChainExpr, Program};
use swc_ecma_parser::{lexer::Lexer, EsSyntax, Parser, StringInput, Syntax};
use swc_ecma_visit::{Visit, VisitWith};

#[derive(Debug, Default, Serialize)]
pub struct Es2020FeatureReport {
    pub parse_ok: bool,
    pub optional_chaining: bool,
    pub nullish_coalescing: bool,
    pub bigint_literals: bool,
}

#[derive(Default)]
struct Es2020FeatureVisitor {
    report: Es2020FeatureReport,
}

impl Visit for Es2020FeatureVisitor {
    fn visit_opt_chain_expr(&mut self, node: &OptChainExpr) {
        self.report.optional_chaining = true;
        node.visit_children_with(self);
    }

    fn visit_bin_expr(&mut self, node: &BinExpr) {
        if node.op == BinaryOp::NullishCoalescing {
            self.report.nullish_coalescing = true;
        }

        node.visit_children_with(self);
    }

    fn visit_big_int(&mut self, node: &BigInt) {
        self.report.bigint_literals = true;
        node.visit_children_with(self);
    }
}

fn parse_program(source: &str) -> Result<Program, String> {
    let source_map: Lrc<SourceMap> = Default::default();
    let source_file = source_map.new_source_file(
        FileName::Custom("chakra_es2020_input.js".into()).into(),
        source.to_owned(),
    );

    let lexer = Lexer::new(
        Syntax::Es(EsSyntax {
            jsx: true,
            ..Default::default()
        }),
        Default::default(),
        StringInput::from(&*source_file),
        None,
    );

    let mut parser = Parser::new_from(lexer);
    let program = parser
        .parse_program()
        .map_err(|parse_error| format!("failed to parse source: {parse_error:?}"))?;

    if let Some(first_error) = parser.take_errors().into_iter().next() {
        return Err(format!("source has parser error: {first_error:?}"));
    }

    Ok(program)
}

pub fn analyze_source(source: &str) -> Result<Es2020FeatureReport, String> {
    let program = parse_program(source)?;

    let mut visitor = Es2020FeatureVisitor::default();
    visitor.report.parse_ok = true;
    program.visit_with(&mut visitor);
    Ok(visitor.report)
}

pub fn analyze_source_to_json(source: &str) -> Result<String, String> {
    let report = analyze_source(source)?;
    serde_json::to_string(&report).map_err(|error| format!("failed to serialize report: {error}"))
}
