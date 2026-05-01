"use client";

import { Chess, type Move, type PieceSymbol, type Square } from "chess.js";
import { useCallback, useEffect, useMemo, useRef, useState, type CSSProperties } from "react";

import Board from "@/components/Board/Board";
import { getPieceGlyph, getPieceName } from "@/components/Board/boardUtils";
import GameOverOverlay from "@/components/GameOverOverlay/GameOverOverlay";
import MoveHistory from "@/components/MoveHistory/MoveHistory";
import { useChessEngine } from "@/hooks/useChessEngine";

import styles from "./page.module.css";
import {
  buildMoveHistoryRows,
  collectCapturedPieces,
  ELO_DEFAULT,
  ELO_MAX,
  ELO_MIN,
  formatEvaluationLabel,
  getEloConfig,
  getEvaluationFill,
  getGameResult,
  getUndoPlyCount,
  parseUciMove,
  replayGame,
  STARTING_POSITION_FEN,
  shouldShowLoadingOverlay,
  shouldEngineMove,
  toWhiteCentipawns,
  type GameResult,
  type ParsedUciMove,
} from "./gameUtils";
import {
  BOARD_THEMES,
  getBoardTheme,
  getBoardThemeStyle,
  type ThemeKey,
} from "./themes";

const LOADING_PREVIEW_SQUARES = Array.from({ length: 16 }, (_, index) => index);

export default function Home() {
  const {
    error: engineError,
    evaluatePosition,
    isReady,
    isThinking,
    resetEngine,
    searchBestMove,
  } = useChessEngine();
  const [baseFen, setBaseFen] = useState(STARTING_POSITION_FEN);
  const [fenDraft, setFenDraft] = useState(STARTING_POSITION_FEN);
  const [isFenLoaderOpen, setIsFenLoaderOpen] = useState(false);
  const [playedMoves, setPlayedMoves] = useState<ParsedUciMove[]>([]);
  const [elo, setElo] = useState(ELO_DEFAULT);
  const [themeKey, setThemeKey] = useState<ThemeKey>("classic-wood");
  const [evaluation, setEvaluation] = useState<number | null>(null);
  const [positionError, setPositionError] = useState<string | null>(null);
  const [loadingDismissed, setLoadingDismissed] = useState(false);
  const eloRef = useRef(elo);
  const evaluationRequestIdRef = useRef(0);
  const engineSearchIdRef = useRef(0);

  const theme = useMemo(() => getBoardTheme(themeKey), [themeKey]);
  const themeStyle = useMemo(() => getBoardThemeStyle(theme), [theme]);
  const game = useMemo(() => replayGame(playedMoves, baseFen), [baseFen, playedMoves]);
  const history = useMemo(() => game.history({ verbose: true }) as Move[], [game]);
  const currentFen = game.fen();
  const lastMove = history.at(-1) ?? null;
  const result = getGameResult(game);
  const historyRows = useMemo(() => buildMoveHistoryRows(history), [history]);
  const capturedPieces = useMemo(() => collectCapturedPieces(history), [history]);
  const eloConfig = getEloConfig(elo);
  const evaluationLabel = formatEvaluationLabel(evaluation);
  const evaluationFill = getEvaluationFill(evaluation);
  const moveCount = history.length;
  const undoPlyCount = getUndoPlyCount(currentFen, moveCount);
  const boardDisabled = !isReady || isThinking || result !== null;
  const showLoadingScreen = shouldShowLoadingOverlay({
    engineError,
    isReady,
    loadingDismissed,
  });
  const loadingScreenClosing = engineError === null && isReady && !loadingDismissed;
  const statusLabel = getStatusLabel({
    engineError,
    game,
    isReady,
    isThinking,
    result,
  });

  useEffect(() => {
    eloRef.current = elo;
  }, [elo]);

  useEffect(() => {
    const handleKeyDown = (event: KeyboardEvent) => {
      if (!(event.altKey && event.shiftKey && event.code === "KeyF")) {
        return;
      }

      const target = event.target;
      if (
        target instanceof HTMLElement &&
        (target instanceof HTMLInputElement ||
          target instanceof HTMLTextAreaElement ||
          target instanceof HTMLSelectElement ||
          target.isContentEditable)
      ) {
        return;
      }

      event.preventDefault();
      setIsFenLoaderOpen((currentValue) => !currentValue);
    };

    window.addEventListener("keydown", handleKeyDown);

    return () => {
      window.removeEventListener("keydown", handleKeyDown);
    };
  }, []);

  useEffect(() => {
    if (!isReady) {
      const frameId = window.requestAnimationFrame(() => {
        setLoadingDismissed(false);
      });

      return () => window.cancelAnimationFrame(frameId);
    }

    const timeoutId = window.setTimeout(() => {
      setLoadingDismissed(true);
    }, 320);

    return () => {
      window.clearTimeout(timeoutId);
    };
  }, [isReady]);

  useEffect(() => {
    if (!isReady) {
      return;
    }

    const requestId = evaluationRequestIdRef.current + 1;
    evaluationRequestIdRef.current = requestId;

    void evaluatePosition(currentFen)
      .then((score) => {
        if (evaluationRequestIdRef.current !== requestId) {
          return;
        }

        setEvaluation(toWhiteCentipawns(score, currentFen));
        setPositionError(null);
      })
      .catch((error: unknown) => {
        if (evaluationRequestIdRef.current !== requestId) {
          return;
        }

        setPositionError(
          error instanceof Error ? error.message : "Failed to evaluate the current position.",
        );
      });
  }, [currentFen, evaluatePosition, isReady]);

  useEffect(() => {
    if (!isReady || result !== null || !shouldEngineMove(currentFen)) {
      return;
    }

    const searchId = engineSearchIdRef.current + 1;
    const searchFen = currentFen;
    const currentElo = getEloConfig(eloRef.current);
    engineSearchIdRef.current = searchId;

    void searchBestMove(searchFen, currentElo.depth, currentElo.timeMs)
      .then((bestMoveUci) => {
        if (engineSearchIdRef.current !== searchId) {
          return;
        }

        const parsedMove = parseUciMove(bestMoveUci);
        if (parsedMove === null) {
          throw new Error(`Engine returned an invalid move: "${bestMoveUci}"`);
        }

        const nextGame = new Chess(searchFen);
        const move = nextGame.move(parsedMove);

        if (move === null) {
          throw new Error(`Engine returned an illegal move for the current board: "${bestMoveUci}"`);
        }

        setPlayedMoves((currentMoves) => {
          if (replayGame(currentMoves, baseFen).fen() !== searchFen) {
            return currentMoves;
          }

          return [...currentMoves, parsedMove];
        });
      })
      .catch((error: unknown) => {
        if (engineSearchIdRef.current !== searchId) {
          return;
        }

        setPositionError(
          error instanceof Error ? error.message : "Failed to apply the engine response.",
        );
      });
  }, [baseFen, currentFen, isReady, result, searchBestMove]);

  const cancelPendingEngineWork = useCallback(() => {
    engineSearchIdRef.current += 1;
    evaluationRequestIdRef.current += 1;

    if (!isThinking) {
      return;
    }

    void resetEngine().catch((error: unknown) => {
      setPositionError(
        error instanceof Error ? error.message : "Failed to restart the chess engine worker.",
      );
    });
  }, [isThinking, resetEngine]);

  const handleMove = ({
    from,
    to,
    promotion,
  }: {
    from: Square;
    to: Square;
    promotion?: PieceSymbol;
  }) => {
    if (boardDisabled) {
      return;
    }

    const nextGame = new Chess(currentFen);
    const move = nextGame.move({
      from,
      to,
      ...(promotion ? { promotion } : {}),
    });

    if (move === null) {
      return;
    }

    setPlayedMoves((currentMoves) => [
      ...currentMoves,
      { from, to, ...(promotion ? { promotion } : {}) },
    ]);
  };

  const handleNewGame = useCallback(() => {
    cancelPendingEngineWork();
    setBaseFen(STARTING_POSITION_FEN);
    setFenDraft(STARTING_POSITION_FEN);
    setEvaluation(0);
    setPositionError(null);
    setPlayedMoves([]);
  }, [cancelPendingEngineWork]);

  const handleUndo = useCallback(() => {
    if (undoPlyCount === 0) {
      return;
    }

    cancelPendingEngineWork();
    setEvaluation(null);
    setPositionError(null);
    setPlayedMoves((currentMoves) =>
      currentMoves.slice(0, Math.max(0, currentMoves.length - undoPlyCount)),
    );
  }, [cancelPendingEngineWork, undoPlyCount]);

  const handleLoadFen = useCallback(() => {
    const nextFen = fenDraft.trim();

    if (nextFen.length === 0) {
      setPositionError("Enter a valid FEN before loading a custom position.");
      return;
    }

    try {
      const normalizedFen = new Chess(nextFen).fen();
      cancelPendingEngineWork();
      setBaseFen(normalizedFen);
      setFenDraft(normalizedFen);
      setEvaluation(null);
      setPositionError(null);
      setPlayedMoves([]);
    } catch (error: unknown) {
      setPositionError(
        error instanceof Error ? error.message : "Failed to load the supplied FEN.",
      );
    }
  }, [cancelPendingEngineWork, fenDraft]);

  return (
    <main className={styles.page} data-theme={theme.key} style={themeStyle}>
      <div aria-hidden="true" className={styles.backdrop} />

      {showLoadingScreen ? (
        <div
          aria-live="polite"
          className={`${styles.loadingOverlay} ${
            loadingScreenClosing ? styles.loadingOverlayExit : ""
          }`}
          role="status"
        >
          <div className={styles.loadingContent}>
            <h2 className={styles.loadingLogo}>Claudefish</h2>
            <p className={styles.loadingSubtitle}>{theme.loadingLabel}</p>
            <div aria-hidden="true" className={styles.loadingPreview}>
              {LOADING_PREVIEW_SQUARES.map((squareIndex) => (
                <span
                  className={
                    squareIndex % 2 === 0
                      ? styles.loadingPreviewLight
                      : styles.loadingPreviewDark
                  }
                  key={squareIndex}
                />
              ))}
            </div>
            <div className={styles.loadingStatusRow}>
              <span aria-hidden="true" className={styles.spinner} />
              <span>Initializing engine</span>
            </div>
          </div>
        </div>
      ) : null}

      <header className={styles.topBar}>
        <h1 className={styles.logo}>
          <span aria-hidden="true" className={styles.logoIcon}>
            ♔
          </span>
          Claudefish
        </h1>
        <nav className={styles.topBarControls} aria-label="Game controls">
          <div className={styles.controlGroup} role="group" aria-label="Board theme">
            <span className={styles.controlLabel}>Theme</span>
            <div className={styles.themePills}>
              {BOARD_THEMES.map((option) => {
                const isSelected = option.key === themeKey;

                return (
                  <button
                    aria-label={option.label}
                    aria-pressed={isSelected}
                    className={`${styles.themePill} ${
                      isSelected ? styles.themePillActive : ""
                    }`}
                    key={option.key}
                    onClick={() => setThemeKey(option.key)}
                    style={
                      {
                        "--swatch-light": option.colors.squareLight,
                        "--swatch-dark": option.colors.squareDark,
                      } as CSSProperties
                    }
                    title={option.label}
                    type="button"
                  >
                    <span aria-hidden="true" className={styles.swatch} />
                  </button>
                );
              })}
            </div>
          </div>

          <div className={styles.controlGroup} role="group" aria-label="Difficulty">
            <span className={styles.controlLabel}>
              Strength <span className={styles.eloValue}>{eloConfig.elo}</span>
            </span>
            <div className={styles.eloSliderRow}>
              <span className={styles.eloBound}>{ELO_MIN}</span>
              <input
                aria-label={`Engine strength: ELO ${elo}`}
                className={styles.eloSlider}
                max={ELO_MAX}
                min={ELO_MIN}
                onChange={(event) => setElo(Number(event.target.value))}
                step={50}
                type="range"
                value={elo}
              />
              <span className={styles.eloBound}>{ELO_MAX}</span>
            </div>
          </div>

          <div className={styles.actionGroup}>
            <button
              className={styles.actionBtn}
              disabled={undoPlyCount === 0}
              onClick={handleUndo}
              title="Undo last move"
              type="button"
            >
              Undo
            </button>
            <button
              className={styles.actionBtn}
              onClick={handleNewGame}
              title="Start new game"
              type="button"
            >
              New
            </button>
            <button
              aria-expanded={isFenLoaderOpen}
              aria-label="Toggle developer FEN loader"
              className={`${styles.devBtn} ${
                isFenLoaderOpen ? styles.devBtnActive : ""
              }`}
              onClick={() => setIsFenLoaderOpen((currentValue) => !currentValue)}
              title="FEN loader (⌥⇧F)"
              type="button"
            >
              <span aria-hidden="true">⚙</span>
            </button>
          </div>
        </nav>
      </header>

      {isFenLoaderOpen ? (
        <div className={styles.fenDock}>
          <label className={styles.fenLabel} htmlFor="fen-loader-input">
            FEN
          </label>
          <input
            autoCapitalize="off"
            autoComplete="off"
            autoCorrect="off"
            className={styles.fenInput}
            id="fen-loader-input"
            name="fen-loader-input"
            onChange={(event) => setFenDraft(event.target.value)}
            placeholder="Paste a FEN string"
            spellCheck={false}
            type="text"
            value={fenDraft}
          />
          <button className={styles.fenLoadBtn} onClick={handleLoadFen} type="button">
            Load
          </button>
          <span className={styles.fenShortcut}>⌥⇧F</span>
        </div>
      ) : null}

      <div className={styles.gameArea}>
        <div className={styles.boardColumn}>
          <div className={styles.playerBar}>
            <div className={styles.playerInfo}>
              <span
                className={`${styles.playerAvatar} ${styles.avatarBlack}`}
                aria-hidden="true"
              >
                ♚
              </span>
              <span className={styles.playerName}>Claudefish</span>
              <span className={styles.playerTag}>{eloConfig.label}</span>
              {isThinking ? (
                <span className={styles.thinkingDot} aria-label="Thinking" />
              ) : null}
            </div>
            <div className={styles.captures} aria-label="Pieces captured by Black">
              {capturedPieces.black.map((piece, index) => (
                <span
                  aria-label={getPieceName(piece)}
                  className={`${styles.capturedPiece} ${
                    piece === piece.toUpperCase() ? styles.capWhite : styles.capBlack
                  }`}
                  key={`bc-${piece}-${index}`}
                >
                  {getPieceGlyph(piece)}
                </span>
              ))}
            </div>
          </div>

          <div className={styles.boardRow}>
            <div
              aria-label={`Evaluation bar: ${evaluationLabel}`}
              className={styles.evalBar}
              style={
                {
                  "--eval-fill": `${evaluationFill}%`,
                } as CSSProperties
              }
            >
              <div className={styles.evalTrack}>
                <div className={styles.evalBlackFill} />
                <div className={styles.evalWhiteFill} />
                <div className={styles.evalDivider} />
              </div>
              <span className={styles.evalLabel}>{evaluationLabel}</span>
            </div>
            <div className={styles.boardSurface}>
              <Board
                disabled={boardDisabled}
                fen={currentFen}
                lastMove={lastMove}
                onMove={handleMove}
              />
              {result !== null ? (
                <GameOverOverlay onNewGame={handleNewGame} result={result} />
              ) : null}
            </div>
          </div>

          <div className={styles.playerBar}>
            <div className={styles.playerInfo}>
              <span
                className={`${styles.playerAvatar} ${styles.avatarWhite}`}
                aria-hidden="true"
              >
                ♔
              </span>
              <span className={styles.playerName}>You</span>
            </div>
            <div className={styles.captures} aria-label="Pieces captured by White">
              {capturedPieces.white.map((piece, index) => (
                <span
                  aria-label={getPieceName(piece)}
                  className={`${styles.capturedPiece} ${
                    piece === piece.toUpperCase() ? styles.capWhite : styles.capBlack
                  }`}
                  key={`wc-${piece}-${index}`}
                >
                  {getPieceGlyph(piece)}
                </span>
              ))}
            </div>
          </div>

          <div className={styles.statusLine}>
            {isThinking ? (
              <span aria-hidden="true" className={styles.spinner} />
            ) : null}
            <span className={styles.statusText}>{statusLabel}</span>
            <span className={styles.statusEval}>{evaluationLabel}</span>
          </div>
        </div>

        <aside className={styles.movePanel}>
          <MoveHistory rows={historyRows} />
        </aside>
      </div>

      {positionError !== null || engineError !== null ? (
        <div className={styles.errorToast} role="alert">
          <span aria-hidden="true" className={styles.errorDot} />
          <span>{positionError ?? engineError}</span>
        </div>
      ) : null}
    </main>
  );
}

function getStatusLabel({
  engineError,
  game,
  isReady,
  isThinking,
  result,
}: {
  engineError: string | null;
  game: Chess;
  isReady: boolean;
  isThinking: boolean;
  result: GameResult | null;
}): string {
  if (engineError !== null) {
    return engineError;
  }

  if (!isReady) {
    return "Initializing Claudefish…";
  }

  if (result !== null) {
    return result.title;
  }

  if (isThinking) {
    return "Claudefish is calculating a reply.";
  }

  if (game.isCheck()) {
    return game.turn() === "w"
      ? "Your king is in check."
      : "Claudefish is in check.";
  }

  return game.turn() === "w" ? "Your move." : "Claudefish to move.";
}
