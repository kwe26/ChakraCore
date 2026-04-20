use serde::Serialize;
use swc_common::{sync::Lrc, util::take::Take, FileName, SourceMap, DUMMY_SP};
use swc_ecma_ast::{
    AssignExpr, AssignOp, AssignTarget, BinExpr, BinaryOp, Expr, ParenExpr, Program,
};
use swc_ecma_codegen::to_code;
use swc_ecma_parser::{lexer::Lexer, EsSyntax, Parser, StringInput, Syntax};
use swc_ecma_visit::{Visit, VisitMut, VisitMutWith, VisitWith};

#[derive(Debug, Default, Serialize)]
pub struct Es2021FeatureReport {
    pub parse_ok: bool,
    pub logical_and_assignment: bool,
    pub logical_or_assignment: bool,
    pub logical_nullish_assignment: bool,
}

#[derive(Default)]
struct Es2021FeatureVisitor {
    report: Es2021FeatureReport,
}

impl Visit for Es2021FeatureVisitor {
    fn visit_assign_expr(&mut self, node: &AssignExpr) {
        match node.op {
            AssignOp::AndAssign => self.report.logical_and_assignment = true,
            AssignOp::OrAssign => self.report.logical_or_assignment = true,
            AssignOp::NullishAssign => self.report.logical_nullish_assignment = true,
            _ => {}
        }

        node.visit_children_with(self);
    }
}

fn parse_program(source: &str) -> Result<Program, String> {
    let source_map: Lrc<SourceMap> = Default::default();
    let source_file = source_map.new_source_file(
        FileName::Custom("chakra_es2021_input.js".into()).into(),
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

pub fn analyze_source(source: &str) -> Result<Es2021FeatureReport, String> {
    let program = parse_program(source)?;

    let mut visitor = Es2021FeatureVisitor::default();
    visitor.report.parse_ok = true;
    program.visit_with(&mut visitor);
    Ok(visitor.report)
}

pub fn analyze_source_to_json(source: &str) -> Result<String, String> {
    let report = analyze_source(source)?;
    serde_json::to_string(&report).map_err(|error| format!("failed to serialize report: {error}"))
}

struct LogicalAssignmentLowerer;

impl VisitMut for LogicalAssignmentLowerer {
    fn visit_mut_expr(&mut self, node: &mut Expr) {
        node.visit_mut_children_with(self);

        let assign_expr = match node {
            Expr::Assign(assign_expr) => assign_expr,
            _ => return,
        };

        let binary_operator = match assign_expr.op {
            AssignOp::AndAssign => BinaryOp::LogicalAnd,
            AssignOp::OrAssign => BinaryOp::LogicalOr,
            AssignOp::NullishAssign => BinaryOp::NullishCoalescing,
            _ => return,
        };

        let left_simple = match assign_expr.left.clone() {
            AssignTarget::Simple(simple) => simple,
            AssignTarget::Pat(_) => return,
        };

        let left_expr: Box<Expr> = left_simple.clone().into();
        let right_value = assign_expr.right.take();
        let plain_assignment = Expr::Assign(AssignExpr {
            span: assign_expr.span,
            op: AssignOp::Assign,
            left: AssignTarget::Simple(left_simple),
            right: right_value,
        });

        let lowered = Expr::Bin(BinExpr {
            span: assign_expr.span,
            op: binary_operator,
            left: left_expr,
            right: Box::new(Expr::Paren(ParenExpr {
                span: DUMMY_SP,
                expr: Box::new(plain_assignment),
            })),
        });

        *node = lowered;
    }
}

pub fn transform_source_for_runtime(source: &str) -> Result<String, String> {
    let mut program = parse_program(source)?;
    program.visit_mut_with(&mut LogicalAssignmentLowerer);

    Ok(to_code(&program))
}
