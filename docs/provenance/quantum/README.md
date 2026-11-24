# CAB Quantum

FultaArc reads Microsoft Cabinet folders compressed with **Quantum** (`typeCompress` 2, windows 2^10 to 2^21) with
`src/codec/quantum.c`, a decoder written in FultaArc's own clean room. Microsoft never published Quantum, and the
public Quantum decoders are copyleft or of unverified licence, so none of them was read: the room derived the method
from zero, black-box, by giving chosen input to Microsoft's own cabinet reader and observing what came out, and
confirmed it on cabinets made by Microsoft's own encoder and shipped in Microsoft products. Four pages document it:

| Page | What it answers |
|---|---|
| [`format.md`](format.md) | where Quantum data sits in a cabinet: the window parameter, blocks, folders across cabinets |
| [`algorithm.md`](algorithm.md) | how the data is decoded: the arithmetic coder, the symbols, the adaptive tables |
| [`derivation.md`](derivation.md) | how every rule in those two pages was found, observation by observation |
| [`provenance.md`](provenance.md) | what FultaArc contains, which sources were used on what terms, which were kept out |
