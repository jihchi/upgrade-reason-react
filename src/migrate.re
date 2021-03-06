open Belt;

open Refmt_api.Migrate_parsetree;
open Ast_404;
open Ast_helper;
open Ast_mapper;
open Asttypes;
open Parsetree;
open Longident;

let unitExpr = Exp.construct({loc: Location.none, txt: Lident("()")}, None);

let hasWeirdHyphens = ({txt}) => {
  switch (txt) {
  | Lident(name) when String.length(name) > 5
    && name.[0] === 'd'
    && name.[1] === 'a'
    && name.[2] === 't'
    && name.[3] === 'a'
    && name.[4] === '-'
    => true /* aria-foo is fine. Transformed later to ariaFoo */
  | _ => false
  }
};

let camelCaseAriaIfExists = (label) => {
  let len = String.length(label);
  if (len > 5
    && label.[0] === 'a'
    && label.[1] === 'r'
    && label.[2] === 'i'
    && label.[3] === 'a'
    && label.[4] === '-') {
    "aria" ++ String.capitalize(String.sub(label, 5, len - 5))
  } else {
    label
  }
};

let refactorMapper = {
  ...default_mapper,
  module_expr: (mapper, item) => {
    switch (item) {
    /* ReactEventRe.UI => ReactEvent.UI */
    | {pmod_desc: Pmod_ident({loc, txt: Ldot(Lident("ReactEventRe"), eventModuleName)})} =>
      {...item, pmod_desc: Pmod_ident({loc, txt: Ldot(Lident("ReactEvent"), eventModuleName)})}
    | _ => default_mapper.module_expr(mapper, item)
    }
  },
  structure_item: (mapper, item) => {
    switch (item) {
    /* open ReactEventRe => open ReactEvent */
    | {pstr_desc: Pstr_open({popen_lid: {loc, txt: Lident("ReactEventRe")}} as o)} =>
      {...item, pstr_desc: Pstr_open({...o, popen_lid: {loc, txt: Lident("ReactEvent")}})}
    | _ => default_mapper.structure_item(mapper, item)
    }
  },
  expr: (mapper, item) =>
    switch (item) {
    /* ReactEventRe.(...) => ReactEvent.(...) */
    | {pexp_desc: Pexp_open(overrideFlag, {loc, txt: Lident("ReactEventRe")}, e)} =>
      {...item, pexp_desc: Pexp_open(overrideFlag, {loc, txt: Lident("ReactEvent")}, mapper.expr(mapper, e))}
    /* ReactEventRe.*._type => ReactEvent.*.type_ */
    | {pexp_desc: Pexp_ident({loc, txt: Ldot(Ldot(Lident("ReactEventRe"), eventModuleName), "_type")})} =>
      {...item, pexp_desc: Pexp_ident({loc, txt: Ldot(Ldot(Lident("ReactEvent"), eventModuleName), "type_")})}
    /* e |> ReactEventRe.Mouse.preventDefault => e->ReactEvent.Mouse.preventDefault */
    | {pexp_desc: Pexp_apply(
        {pexp_desc: Pexp_ident({loc, txt: Lident("|>")})} as f,
        [
          (Nolabel, e),
          (Nolabel, {pexp_desc: Pexp_ident({
            loc: loc2,
            txt: Ldot(
              Ldot(Lident("ReactEventRe"), _eventModuleName),
              _callName
            )
          })} as call)
        ]
      )} =>
      {...item, pexp_desc: Pexp_apply(
        {...f, pexp_desc: Pexp_ident({loc, txt: Lident("|.")})},
        [(Nolabel, mapper.expr(mapper, e)), (Nolabel, mapper.expr(mapper, call))]
      )}
    /* e |> ReactEventRe.Form.target |> ReactDOMRe.domElementToObj => e->ReactEvent.Form.target */
    /* |--- or anything similar ---| */
    | {pexp_desc: Pexp_apply(
        {pexp_desc: Pexp_ident({loc, txt: Lident("|>" | "|.")})} as f,
        [
          (Nolabel, e),
          (Nolabel, {pexp_desc: Pexp_ident({
            txt: Ldot(Lident("ReactDOMRe"), "domElementToObj"),
          })})
        ]
      )}
    /* ReactDOMRe.domElementToObj(e |> ReactEventRe.Form.target) => e->ReactEvent.Form.target */
    /*                            |--- or anything similar ---| */
    | {pexp_desc: Pexp_apply(
        {pexp_desc: Pexp_ident({loc, txt: Ldot(Lident("ReactDOMRe"), "domElementToObj")})} as f,
        [(Nolabel, e)]
      )} =>
        switch (e) {
        | {pexp_desc: Pexp_apply(
            {pexp_desc: Pexp_ident({loc, txt: Lident("|>" | "|.")})} as f,
            [
              (Nolabel, e2),
              (Nolabel, {pexp_desc: Pexp_ident({
                loc: loc2,
                txt: Ldot(Ldot(Lident("ReactEventRe"), eventModuleName), callName),
              })} as call)
            ]
          )} =>
          {...e, pexp_desc: Pexp_apply(
            {...f, pexp_desc: Pexp_ident({loc, txt: Lident("|.")})},
            [
              (Nolabel, mapper.expr(mapper, e2)),
              (Nolabel, {...call, pexp_desc: Pexp_ident({
                loc: loc2,
                txt: Ldot(Ldot(Lident("ReactEvent"), eventModuleName), callName),
              })})
            ]
          )}
        | {pexp_desc: Pexp_apply(
            {pexp_desc: Pexp_ident({loc, txt: Ldot(Ldot(Lident("ReactEventRe"), eventModuleName), callName)})} as f,
            [(Nolabel, e2)]
          )} =>
          {...e, pexp_desc: Pexp_apply(
            {...f, pexp_desc: Pexp_ident({loc, txt: Ldot(Ldot(Lident("ReactEvent"), eventModuleName), callName)})},
            [(Nolabel, mapper.expr(mapper, e2))]
          )}
        | _ => default_mapper.expr(mapper, item)
        };
    /* ReactEventRe => ReactEvent */
    /* this should be at the end of ReactEventRe codemods, since pattern matching order matters */
    | {pexp_desc: Pexp_ident({loc, txt: Lident("ReactEventRe")})} =>
      {...item, pexp_desc: Pexp_ident({loc, txt: Lident("ReactEvent")})}
    | {pexp_desc: Pexp_ident({loc, txt: Ldot(Ldot(Lident("ReactEventRe"), eventModuleName), call)})} =>
      {...item, pexp_desc: Pexp_ident({loc, txt: Ldot(Ldot(Lident("ReactEvent"), eventModuleName), call)})}

    /* ([@JSX] div(~_to="a", ~children=foo, ())) => <div to_="a"> ...foo </div> */
    | {
        pexp_desc: Pexp_apply({pexp_desc: Pexp_ident({loc, txt: Lident(domTag)})}, [_, ..._] as props),
        pexp_attributes: [_, ..._] as pexp_attributes
      }
      when List.some(pexp_attributes, (({txt}, _)) => txt == "JSX") =>
      let newProps = List.map(props, ((label, expr)) => {
        let newLabel = switch (label) {
        | Labelled("_open") => Labelled("open_")
        | Labelled("_type") => Labelled("type_")
        | Labelled("_begin") => Labelled("begin_")
        | Labelled("_end") => Labelled("end_")
        | Labelled("_in") => Labelled("in_")
        | Labelled("_to") => Labelled("to_")
        | label => label
        };
        (newLabel, mapper.expr(mapper, expr))
      });
      {...item, pexp_desc: Pexp_apply(Exp.ident({loc, txt: Lident(domTag)}), newProps)}

    /* ReasonReact.createDomElement("div", {"a": b}, bar) => <div ~a="b"> ...bar </div> */
    | {pexp_desc: Pexp_apply(
        {pexp_desc: Pexp_ident({loc, txt: Ldot(Lident("ReasonReact"), "createDomElement")})},
        [
          (Nolabel, {pexp_desc: Pexp_constant(Pconst_string(domTag, None))}) as tag,
          (Nolabel, props),
          (Nolabel, children),
        ]
      )} =>
      let newChildren = mapper.expr(mapper, children);
      switch (props) {
      /* if props are like {"a": b} */
      | {pexp_desc: Pexp_extension((
          {txt: "bs.obj"},
          PStr([{pstr_desc: Pstr_eval({pexp_desc: Pexp_record(fields, None)}, attrs)}])
        ))}
        when List.every(fields, ((name, value)) => !hasWeirdHyphens(name)) =>
        /* has some data-foo attributes? Bail and in catch-all branch below */
        /* otherwise, become <div ~a="b"> ...bar </div> */
        let newProps = List.map(fields, (({txt}, value)) => {
          let label = Longident.last(txt) |> camelCaseAriaIfExists;
          (Labelled(label), mapper.expr(mapper, value))
        });
        {
          ...item,
          pexp_attributes: [({loc: Location.none, txt: "JSX"}, PStr([])), ...item.pexp_attributes],
          pexp_desc: Pexp_apply(
            Exp.ident({loc: Location.none, txt: Lident(domTag)}),
            newProps @ [(Labelled("children"), newChildren), (Nolabel, unitExpr)]
          )
        }
      /* if props is Js.Obj.empty() */
      | {pexp_desc: Pexp_apply(
          {pexp_desc: Pexp_ident({txt: Ldot(Ldot(Lident("Js"), "Obj"), "empty")})},
          [(Nolabel, {pexp_desc: Pexp_construct({txt: Lident("()")}, None)})]
        )} =>
        {
          ...item,
          pexp_attributes: [({loc: Location.none, txt: "JSX"}, PStr([])), ...item.pexp_attributes],
          pexp_desc: Pexp_apply(
            Exp.ident({loc: Location.none, txt: Lident(domTag)}),
            [
              (Labelled("children"), newChildren),
              (Nolabel, unitExpr)
            ]
          )
        }
      /* if props is anything else */
      | e =>
        /* keep things as ReactDOMRe.createElementVariadic("div", ~props=ReactDOMRe.objToDOMProps({"a": b}), bar) */
        {
          ...item,
          pexp_desc: Pexp_apply(
            Exp.ident({loc, txt: Ldot(Lident("ReactDOMRe"), "createElementVariadic")}),
            [
              tag,
              (Labelled("props"), Exp.apply(
                Exp.ident({loc: Location.none, txt: Ldot(Lident("ReactDOMRe"), "objToDOMProps")}),
                [(Nolabel, mapper.expr(mapper, props))]
              )),
              (Nolabel, newChildren),
            ]
          )
        }
      };
    | anythingElse => default_mapper.expr(mapper, anythingElse)
    }
};

switch (Sys.argv) {
| [||]
| [|_|]
| [|_, "help" | "-help" | "--help"|] =>
  print_endline("Usage: pass a list of .re files you'd like to convert.")
| arguments =>
  let validFiles =
    Array.slice(arguments, ~offset=1, ~len=Array.length(arguments) - 1)
    |. Array.keep(file => {
      let isReason = Filename.check_suffix(file, ".re");
      if (isReason) {
        if (Sys.file_exists(file)) {
          true
        } else {
          print_endline(file ++ " doesn't exist. Skipped.");
          false
        }
      } else {
        false
      }
    });
  switch (validFiles) {
  | [||] => print_endline("You didn't pass any Reason file to convert.");
  | files =>
    Array.forEach(files, file => {
      let ic = open_in_bin(file);
      let lexbuf = Lexing.from_channel(ic);
      let (ast, comments) =
      Refmt_api.Reason_toolchain.RE.implementation_with_comments(
        lexbuf
        );
      let newAst = refactorMapper.structure(refactorMapper, ast);
      let target = file;
      let oc = open_out_bin(target);
      let formatter = Format.formatter_of_out_channel(oc);
      Refmt_api.Reason_toolchain.RE.print_implementation_with_comments(
        formatter,
        (newAst, comments)
        );
      Format.print_flush();
      close_out(oc);
    });
    print_endline(
      "\nDone! Please build your project again. It's possible that it fails; if so, it's expected. Check the changes this script made."
    );
  }
};
