/*
 * Copyright (c) 2015-present, Facebook, Inc.
 * All rights reserved.
 *
 * vim: set ft=rust:
 * vim: set ft=reason:
 */

open NuclideReasonCommon;

open StringUtils;

open Atom;

/**
 * TODO: Strip trailing whitespace in this as well (though that will eventually
 * be done in the formatting toolchain anyways).
 *
 * @param (Array.t string) standard output formatting of file.
 * @param int curCursorRow Current cursor row before formatting.
 * @param int curCursorColumn Current cursor column before formatting.
 * @return Nuclide.FileFormat.result
 */
let characterIndexForPositionInString stdOutLines (curCursorRow, curCursorColumn) => {
  let result = {contents: ""};
  let arrLen = Array.length stdOutLines;
  let charCount = {contents: 0};
  let colCount = {contents: 0};
  let rowCount = {contents: 0};
  let finalCharCount = {contents: 0};
  for iArr in 0 to (arrLen - 1) {
    let line = stdOutLines.(iArr);
    let lineLen = String.length line;
    /* We also trim *trailing* whitespace for each line. */
    let lenNotEndingInWhiteSpace = {contents: 0};
    /* No guarantee that each line is actually a single line. */
    for chPos in 0 to (lineLen - 1) {
      let ch = line.[chPos];
      if (ch == '\n' || ch == '\r') {
        rowCount.contents <- rowCount.contents + 1;
        colCount.contents <- 0
      } else {
        colCount.contents <- colCount.contents + 1;
        lenNotEndingInWhiteSpace.contents <- lenNotEndingInWhiteSpace.contents + 1
      };
      charCount.contents <- charCount.contents + 1;
      if (rowCount.contents <= curCursorRow) {
        if (colCount.contents <= curCursorColumn) {
          finalCharCount.contents <- charCount.contents
        }
      }
    };
    result.contents <- result.contents ^ Atom.trimTrailingWhiteSpace line ^ "\n"
  };
  {Nuclide.FileFormat.newCursor: finalCharCount.contents, Nuclide.FileFormat.formatted: result.contents}
};

let formatImpl editor subText isInterface onComplete onFailure => {
  let open Atom.JsonType;
  let stdOutLines = {contents: [||]};
  let stdErrLines = {contents: [||]};
  let fmtPath =
    switch (Atom.Config.get "NuclideReason.pathToReasonfmt") {
      | JsonString pth => pth
      | _ => raise (Invalid_argument "You must setup NuclideReason.pathToReasonfmt in your Atom config")
    };
  let printWidth =
    switch (Atom.Config.get "NuclideReason.printWidth") {
      | JsonNum n => int_of_float n
      | Empty => 110
      | _ => raise (Invalid_argument "NuclideReason.printWidth must be an integer")
    };
  let onStdOut line => stdOutLines.contents <- Array.append stdOutLines.contents [|line|];
  let onStdErr line => stdErrLines.contents <- Array.append stdErrLines.contents [|line|];
  let cursors = Editor.getCursors editor;
  let (origCursorRow, origCursorCol) =
    switch cursors {
      | [] => (0, 0)
      | [firstCursor, ...tl] => Atom.Cursor.getBufferPosition firstCursor
    };
  let onExit code => {
    let formatResult = characterIndexForPositionInString stdOutLines.contents (origCursorRow, origCursorCol);
    let stdErr = String.concat "\n" (Array.to_list stdErrLines.contents);
    onComplete code formatResult stdErr
  };
  let args = [
    "-print-width",
    string_of_int printWidth,
    "-use-stdin",
    "true",
    "-parse",
    "re",
    "-print",
    "re",
    "-is-interface-pp",
    isInterface ? "true" : "false"
  ];
  let proc = Atom.BufferedProcess.create stdout::onStdOut stderr::onStdErr exit::onExit args::args fmtPath;
  let errorTitle = "NuclideReason could not spawn " ^ fmtPath;
  let handleError error handle => {
    NotificationManager.addError options::{...NotificationManager.defaultOptions, detail: error} errorTitle;
    onFailure "Failure!";
    handle ()
  };
  BufferedProcess.onWillThrowError proc handleError;
  /* Underlying child process. */
  let process = BufferedProcess.process proc;
  ChildProcess.writeStdin process subText;
  ChildProcess.endStdin process
};

/**
 * A better way to restore the cursor position is:
 * - If only white space change occured before where the cursor was, place
 * cursor at *new* location after whitespce changes.
 * - As an enhancement, consider insertion/elimination of certain
 * characters/sequences in the same class as white space changes.
 * (extra/removed parens, or even "= fun").
 * - If text before cursor changed in ways beyond "whitespace" changes, fall
 * back to current behavior.
 */
let getFormatting jsEditor jsRange notifySuccess notifyInvalid notifyInfo => {
  let editor = Editor.fromJs jsEditor;
  let maybeFilePath = Editor.getPath editor;
  let (rangeStart, rangeEnd) = Range.fromJs jsRange;
  let buffer = Editor.getBuffer editor;
  let startPosition = Buffer.characterIndexForPosition buffer rangeStart;
  let endPosition = Buffer.characterIndexForPosition buffer rangeEnd;
  let text = Buffer.getText buffer;
  let subText = String.sub text startPosition (endPosition - startPosition);
  /* Including dot */
  let ext =
    switch maybeFilePath {
      | Some filePath => {
          let lastExtensionIndex = String.rindex filePath '.';
          String.sub filePath lastExtensionIndex (String.length filePath - lastExtensionIndex)
        }
      | None => ".re"
    };
  let isInterface = String.compare ".rei" ext == 0;
  let promise = Atom.Promise.create (
    fun resolve reject => {
      let onComplete code formatResult stdErr => {
        let formatResultStr = NuclideJs.FileFormat.toJs formatResult;
        if (not (code = 0.0)) {
          ignore (Js.Unsafe.fun_call notifyInvalid [|Js.Unsafe.inject (Js.string "Syntax Error")|])
        } else if (
          formatResult.formatted === text
        ) {
          let result = String.compare formatResult.formatted text;
          print_string "printing result:";
          print_int result;
          let msg = "Already Formatted";
          ignore (Js.Unsafe.fun_call notifyInfo [|Js.Unsafe.inject (Js.string msg)|])
        } else {
          let result = String.compare formatResult.formatted text;
          print_string "printing result:";
          print_int result;
          let msg = "Format: Success";
          ignore (Js.Unsafe.fun_call notifySuccess [|Js.Unsafe.inject (Js.string msg)|])
        };
        code = 0.0 ? resolve formatResultStr : reject stdErr
      };
      formatImpl editor subText isInterface onComplete reject
    }
  );
  Atom.Promise.toJs promise
};

